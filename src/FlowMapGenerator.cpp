#include "FlowMapGenerator.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <queue>
#include <cmath>

bool FlowMapGenerator::init(VkDevice device, VmaAllocator allocator,
                            VkCommandPool commandPool, VkQueue queue) {
    this->device = device;
    this->allocator = allocator;
    this->commandPool = commandPool;
    this->queue = queue;

    if (!createSampler()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create flow map sampler");
        return false;
    }

    return true;
}

void FlowMapGenerator::destroy(VkDevice device, VmaAllocator allocator) {
    if (flowMapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, flowMapSampler, nullptr);
        flowMapSampler = VK_NULL_HANDLE;
    }

    if (flowMapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, flowMapView, nullptr);
        flowMapView = VK_NULL_HANDLE;
    }

    if (flowMapImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, flowMapImage, flowMapAllocation);
        flowMapImage = VK_NULL_HANDLE;
        flowMapAllocation = VK_NULL_HANDLE;
    }

    flowData.clear();
    signedDistanceField.clear();
}

bool FlowMapGenerator::createImage(uint32_t resolution) {
    // Clean up existing image if resolution changed
    if (flowMapImage != VK_NULL_HANDLE && currentResolution != resolution) {
        vkDestroyImageView(device, flowMapView, nullptr);
        vmaDestroyImage(allocator, flowMapImage, flowMapAllocation);
        flowMapImage = VK_NULL_HANDLE;
        flowMapView = VK_NULL_HANDLE;
    }

    if (flowMapImage != VK_NULL_HANDLE) {
        return true; // Already created at correct resolution
    }

    currentResolution = resolution;

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {resolution, resolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &flowMapImage, &flowMapAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create flow map image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = flowMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &flowMapView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create flow map view");
        return false;
    }

    SDL_Log("Flow map created: %ux%u", resolution, resolution);
    return true;
}

bool FlowMapGenerator::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 4.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    return vkCreateSampler(device, &samplerInfo, nullptr, &flowMapSampler) == VK_SUCCESS;
}

void FlowMapGenerator::uploadToGPU() {
    if (flowData.empty() || flowMapImage == VK_NULL_HANDLE) return;

    // Create staging buffer
    VkDeviceSize imageSize = currentResolution * currentResolution * 4; // RGBA8

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingInfo;

    if (vmaCreateBuffer(allocator, &bufferInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, &stagingInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for flow map");
        return;
    }

    // Convert float data to RGBA8
    uint8_t* pixels = static_cast<uint8_t*>(stagingInfo.pMappedData);
    for (size_t i = 0; i < flowData.size(); i++) {
        const glm::vec4& flow = flowData[i];
        pixels[i * 4 + 0] = static_cast<uint8_t>(std::clamp(flow.r, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 1] = static_cast<uint8_t>(std::clamp(flow.g, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 2] = static_cast<uint8_t>(std::clamp(flow.b, 0.0f, 1.0f) * 255.0f);
        pixels[i * 4 + 3] = static_cast<uint8_t>(std::clamp(flow.a, 0.0f, 1.0f) * 255.0f);
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocCommandInfo{};
    allocCommandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCommandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCommandInfo.commandPool = commandPool;
    allocCommandInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocCommandInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Copy staging buffer to flow map with automatic barrier transitions
    Barriers::copyBufferToImage(commandBuffer, stagingBuffer, flowMapImage,
                                currentResolution, currentResolution);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    SDL_Log("Flow map uploaded to GPU");
}

bool FlowMapGenerator::generateFromTerrain(const std::vector<float>& heightData,
                                           uint32_t heightmapSize,
                                           float heightScale,
                                           const Config& config) {
    return generateSlopeBasedFlow(heightData, heightmapSize, heightScale, config);
}

bool FlowMapGenerator::generateRadialFlow(const Config& config, const glm::vec2& center) {
    if (!createImage(config.resolution)) return false;

    currentWorldSize = config.worldSize;
    currentWaterLevel = config.waterLevel;

    uint32_t res = config.resolution;
    flowData.resize(res * res);
    signedDistanceField.resize(res * res);

    float halfSize = config.worldSize * 0.5f;
    glm::vec2 worldCenter = center;

    for (uint32_t y = 0; y < res; y++) {
        for (uint32_t x = 0; x < res; x++) {
            // Convert to world coordinates
            float worldX = (static_cast<float>(x) / res - 0.5f) * config.worldSize;
            float worldZ = (static_cast<float>(y) / res - 0.5f) * config.worldSize;

            // Direction from center (outward flow for lakes)
            glm::vec2 toCenter = worldCenter - glm::vec2(worldX, worldZ);
            float dist = glm::length(toCenter);

            glm::vec2 flowDir(0.0f);
            float speed = 0.0f;

            if (dist > 0.01f) {
                // Circular flow around center (tangent to radius)
                flowDir = glm::normalize(glm::vec2(-toCenter.y, toCenter.x));
                // Speed decreases toward center
                speed = std::min(dist / (config.worldSize * 0.25f), config.maxFlowSpeed);
            }

            // Encode flow direction (from -1,1 to 0,1)
            float encodedX = flowDir.x * 0.5f + 0.5f;
            float encodedZ = flowDir.y * 0.5f + 0.5f;

            // Shore distance (normalized)
            float shoreDist = std::min(dist / config.shoreDistance, 1.0f);

            uint32_t idx = y * res + x;
            flowData[idx] = glm::vec4(encodedX, encodedZ, speed, shoreDist);
            signedDistanceField[idx] = dist;
        }
    }

    uploadToGPU();
    SDL_Log("Generated radial flow map centered at (%.1f, %.1f)", center.x, center.y);
    return true;
}

bool FlowMapGenerator::generateSlopeBasedFlow(const std::vector<float>& heightData,
                                               uint32_t heightmapSize,
                                               float heightScale,
                                               const Config& config) {
    if (!createImage(config.resolution)) return false;

    currentWorldSize = config.worldSize;
    currentWaterLevel = config.waterLevel;

    uint32_t res = config.resolution;
    flowData.resize(res * res);
    signedDistanceField.resize(res * res);

    // Create water mask (where terrain is below water level)
    std::vector<bool> waterMask(res * res);
    std::vector<float> heights(res * res);

    float texelSize = config.worldSize / res;

    for (uint32_t y = 0; y < res; y++) {
        for (uint32_t x = 0; x < res; x++) {
            // Sample height from heightmap (bilinear)
            float u = static_cast<float>(x) / (res - 1);
            float v = static_cast<float>(y) / (res - 1);

            // Bilinear sample from heightmap
            float hx = u * (heightmapSize - 1);
            float hy = v * (heightmapSize - 1);
            uint32_t x0 = static_cast<uint32_t>(hx);
            uint32_t y0 = static_cast<uint32_t>(hy);
            uint32_t x1 = std::min(x0 + 1, heightmapSize - 1);
            uint32_t y1 = std::min(y0 + 1, heightmapSize - 1);
            float fx = hx - x0;
            float fy = hy - y0;

            float h00 = heightData[y0 * heightmapSize + x0];
            float h10 = heightData[y0 * heightmapSize + x1];
            float h01 = heightData[y1 * heightmapSize + x0];
            float h11 = heightData[y1 * heightmapSize + x1];

            float h = (h00 * (1 - fx) * (1 - fy) +
                      h10 * fx * (1 - fy) +
                      h01 * (1 - fx) * fy +
                      h11 * fx * fy);

            float worldHeight = h * heightScale;
            uint32_t idx = y * res + x;
            heights[idx] = worldHeight;
            waterMask[idx] = (worldHeight < config.waterLevel);
        }
    }

    // Compute signed distance field from shore
    computeSignedDistanceField(waterMask);

    // Compute flow directions based on terrain slope
    for (uint32_t y = 0; y < res; y++) {
        for (uint32_t x = 0; x < res; x++) {
            uint32_t idx = y * res + x;

            if (!waterMask[idx]) {
                // Above water - no flow
                flowData[idx] = glm::vec4(0.5f, 0.5f, 0.0f, 1.0f);
                continue;
            }

            // Calculate gradient (slope direction) using Sobel filter
            float dhdx = 0.0f;
            float dhdz = 0.0f;

            // Sample neighbors (with boundary clamping)
            auto sampleHeight = [&](int sx, int sy) -> float {
                int cx = std::clamp(sx, 0, static_cast<int>(res) - 1);
                int cy = std::clamp(sy, 0, static_cast<int>(res) - 1);
                return heights[cy * res + cx];
            };

            // Sobel gradient for smoother results
            float h_l = sampleHeight(x - 1, y);
            float h_r = sampleHeight(x + 1, y);
            float h_t = sampleHeight(x, y - 1);
            float h_b = sampleHeight(x, y + 1);
            float h_tl = sampleHeight(x - 1, y - 1);
            float h_tr = sampleHeight(x + 1, y - 1);
            float h_bl = sampleHeight(x - 1, y + 1);
            float h_br = sampleHeight(x + 1, y + 1);

            dhdx = (h_tr + 2 * h_r + h_br - h_tl - 2 * h_l - h_bl) / (8.0f * texelSize);
            dhdz = (h_bl + 2 * h_b + h_br - h_tl - 2 * h_t - h_tr) / (8.0f * texelSize);

            // Flow direction is downhill (negative gradient)
            glm::vec2 flowDir(-dhdx, -dhdz);
            float slopeMagnitude = glm::length(flowDir);

            // Normalize and apply speed based on slope
            float speed = 0.0f;
            if (slopeMagnitude > 0.001f) {
                flowDir = glm::normalize(flowDir);
                // Speed based on slope steepness
                speed = std::min(slopeMagnitude * config.slopeInfluence, config.maxFlowSpeed);
            } else {
                flowDir = glm::vec2(0.0f);
            }

            // Encode flow direction (from -1,1 to 0,1)
            float encodedX = flowDir.x * 0.5f + 0.5f;
            float encodedZ = flowDir.y * 0.5f + 0.5f;

            // Normalize shore distance
            float normalizedShoreDist = std::min(signedDistanceField[idx] / config.shoreDistance, 1.0f);

            flowData[idx] = glm::vec4(encodedX, encodedZ, speed, normalizedShoreDist);
        }
    }

    uploadToGPU();
    SDL_Log("Generated slope-based flow map (%ux%u) from terrain", res, res);
    return true;
}

void FlowMapGenerator::computeSignedDistanceField(const std::vector<bool>& waterMask) {
    // Jump Flooding Algorithm for SDF computation
    // This is an approximation but very fast for large textures

    uint32_t res = currentResolution;
    std::vector<glm::ivec2> nearestSeed(res * res, glm::ivec2(-1));

    // Initialize seeds at shore boundaries
    for (uint32_t y = 0; y < res; y++) {
        for (uint32_t x = 0; x < res; x++) {
            uint32_t idx = y * res + x;
            bool isWater = waterMask[idx];

            // Check if this is a boundary pixel
            bool isBoundary = false;
            for (int dy = -1; dy <= 1 && !isBoundary; dy++) {
                for (int dx = -1; dx <= 1 && !isBoundary; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = static_cast<int>(x) + dx;
                    int ny = static_cast<int>(y) + dy;
                    if (nx >= 0 && nx < static_cast<int>(res) &&
                        ny >= 0 && ny < static_cast<int>(res)) {
                        bool neighborWater = waterMask[ny * res + nx];
                        if (isWater != neighborWater) {
                            isBoundary = true;
                        }
                    }
                }
            }

            if (isBoundary) {
                nearestSeed[idx] = glm::ivec2(x, y);
            }
        }
    }

    // Jump flooding passes
    for (int step = res / 2; step >= 1; step /= 2) {
        for (uint32_t y = 0; y < res; y++) {
            for (uint32_t x = 0; x < res; x++) {
                uint32_t idx = y * res + x;
                glm::ivec2 bestSeed = nearestSeed[idx];
                float bestDist = (bestSeed.x < 0) ? 1e10f :
                    glm::length(glm::vec2(x - bestSeed.x, y - bestSeed.y));

                // Check neighbors at 'step' distance
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = static_cast<int>(x) + dx * step;
                        int ny = static_cast<int>(y) + dy * step;

                        if (nx >= 0 && nx < static_cast<int>(res) &&
                            ny >= 0 && ny < static_cast<int>(res)) {
                            glm::ivec2 neighborSeed = nearestSeed[ny * res + nx];
                            if (neighborSeed.x >= 0) {
                                float dist = glm::length(glm::vec2(x - neighborSeed.x, y - neighborSeed.y));
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    bestSeed = neighborSeed;
                                }
                            }
                        }
                    }
                }

                nearestSeed[idx] = bestSeed;
            }
        }
    }

    // Convert to world-space distances
    float texelSize = currentWorldSize / res;
    for (uint32_t i = 0; i < res * res; i++) {
        if (nearestSeed[i].x < 0) {
            signedDistanceField[i] = currentWorldSize; // Far from any shore
        } else {
            uint32_t x = i % res;
            uint32_t y = i / res;
            float pixelDist = glm::length(glm::vec2(x - nearestSeed[i].x, y - nearestSeed[i].y));
            signedDistanceField[i] = pixelDist * texelSize;
        }
    }
}

glm::vec4 FlowMapGenerator::sampleFlow(const glm::vec2& worldPos) const {
    if (flowData.empty() || currentResolution == 0) {
        return glm::vec4(0.5f, 0.5f, 0.0f, 1.0f); // No flow
    }

    // Convert world position to UV
    float u = (worldPos.x / currentWorldSize) + 0.5f;
    float v = (worldPos.y / currentWorldSize) + 0.5f;

    // Clamp to valid range
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Bilinear sample
    float fx = u * (currentResolution - 1);
    float fy = v * (currentResolution - 1);
    uint32_t x0 = static_cast<uint32_t>(fx);
    uint32_t y0 = static_cast<uint32_t>(fy);
    uint32_t x1 = std::min(x0 + 1, currentResolution - 1);
    uint32_t y1 = std::min(y0 + 1, currentResolution - 1);
    float wx = fx - x0;
    float wy = fy - y0;

    glm::vec4 s00 = flowData[y0 * currentResolution + x0];
    glm::vec4 s10 = flowData[y0 * currentResolution + x1];
    glm::vec4 s01 = flowData[y1 * currentResolution + x0];
    glm::vec4 s11 = flowData[y1 * currentResolution + x1];

    return s00 * (1 - wx) * (1 - wy) +
           s10 * wx * (1 - wy) +
           s01 * (1 - wx) * wy +
           s11 * wx * wy;
}
