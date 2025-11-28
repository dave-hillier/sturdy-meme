#include "TerrainSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>

using ShaderLoader::loadShaderModule;

bool TerrainSystem::init(const InitInfo& info, const TerrainConfig& cfg) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    texturePath = info.texturePath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    config = cfg;

    // Create all resources
    if (!createCBTBuffer()) return false;
    if (!initializeCBT()) return false;
    if (!createHeightMap()) return false;
    if (!createTerrainTexture()) return false;
    if (!createUniformBuffers()) return false;
    if (!createIndirectBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createDispatcherPipeline()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createSumReductionPipelines()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;
    if (!createShadowPipeline()) return false;

    std::cout << "TerrainSystem initialized with CBT max depth " << config.maxDepth << std::endl;
    return true;
}

void TerrainSystem::destroy(VkDevice device, VmaAllocator allocator) {
    vkDeviceWaitIdle(device);

    // Destroy pipelines
    if (dispatcherPipeline) vkDestroyPipeline(device, dispatcherPipeline, nullptr);
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (sumReductionPrepassPipeline) vkDestroyPipeline(device, sumReductionPrepassPipeline, nullptr);
    if (sumReductionPipeline) vkDestroyPipeline(device, sumReductionPipeline, nullptr);
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);
    if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);

    // Destroy pipeline layouts
    if (dispatcherPipelineLayout) vkDestroyPipelineLayout(device, dispatcherPipelineLayout, nullptr);
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (sumReductionPipelineLayout) vkDestroyPipelineLayout(device, sumReductionPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);

    // Destroy buffers
    if (cbtBuffer) vmaDestroyBuffer(allocator, cbtBuffer, cbtAllocation);
    if (indirectDispatchBuffer) vmaDestroyBuffer(allocator, indirectDispatchBuffer, indirectDispatchAllocation);
    if (indirectDrawBuffer) vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
    }

    // Destroy textures
    if (heightMapSampler) vkDestroySampler(device, heightMapSampler, nullptr);
    if (heightMapView) vkDestroyImageView(device, heightMapView, nullptr);
    if (heightMapImage) vmaDestroyImage(allocator, heightMapImage, heightMapAllocation);

    if (terrainAlbedoSampler) vkDestroySampler(device, terrainAlbedoSampler, nullptr);
    if (terrainAlbedoView) vkDestroyImageView(device, terrainAlbedoView, nullptr);
    if (terrainAlbedoImage) vmaDestroyImage(allocator, terrainAlbedoImage, terrainAlbedoAllocation);

    if (grassFarLODSampler) vkDestroySampler(device, grassFarLODSampler, nullptr);
    if (grassFarLODView) vkDestroyImageView(device, grassFarLODView, nullptr);
    if (grassFarLODImage) vmaDestroyImage(allocator, grassFarLODImage, grassFarLODAllocation);
}

uint32_t TerrainSystem::calculateCBTBufferSize(int maxDepth) {
    // For max depth D, the bitfield needs 2^(D-1) bits = 2^(D-4) bytes
    // Plus the sum reduction tree overhead
    // Total approximately 2^(D-1) bytes
    uint64_t bitfieldBytes = 1ull << (maxDepth - 1);
    // Add some extra for alignment and sum reduction
    return static_cast<uint32_t>(std::min(bitfieldBytes * 2, (uint64_t)64 * 1024 * 1024)); // Cap at 64MB
}

bool TerrainSystem::createCBTBuffer() {
    cbtBufferSize = calculateCBTBufferSize(config.maxDepth);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = cbtBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // Allow host access for initialization
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &cbtBuffer, &cbtAllocation, nullptr) != VK_SUCCESS) {
        std::cerr << "Failed to create CBT buffer" << std::endl;
        return false;
    }

    return true;
}

// Helper functions for CBT bit-level operations (CPU side, matching GLSL and libcbt)
namespace {
    // Get bit field offset for a node at ceiling (maxDepth) level
    uint32_t cbt_NodeBitID_BitField_CPU(uint32_t nodeId, int nodeDepth, int maxDepth) {
        // Ceiling node ID = nodeId << (maxDepth - nodeDepth)
        uint32_t ceilNodeId = nodeId << (maxDepth - nodeDepth);
        // BitField starts at bit offset 2^(maxDepth+1)
        return (2u << maxDepth) + ceilNodeId;
    }

    // Set a single bit in the bitfield (leaf node marker)
    void cbt_HeapWrite_BitField_CPU(std::vector<uint32_t>& heap, uint32_t nodeId, int nodeDepth, int maxDepth) {
        uint32_t bitID = cbt_NodeBitID_BitField_CPU(nodeId, nodeDepth, maxDepth);
        uint32_t heapIndex = bitID >> 5;
        uint32_t localBit = bitID & 31u;
        if (heapIndex < heap.size()) {
            heap[heapIndex] |= (1u << localBit);
        }
    }

    // Node bit ID for sum reduction tree
    uint32_t cbt_NodeBitID_CPU(uint32_t id, int depth, int maxDepth) {
        uint32_t tmp1 = 2u << depth;
        uint32_t tmp2 = uint32_t(1 + maxDepth - depth);
        return tmp1 + id * tmp2;
    }

    int cbt_NodeBitSize_CPU(int depth, int maxDepth) {
        return maxDepth - depth + 1;
    }

    // Read a value from the heap at a specific node position
    uint32_t cbt_HeapRead_CPU(const std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth) {
        uint32_t bitOffset = cbt_NodeBitID_CPU(id, depth, maxDepth);
        int bitCount = cbt_NodeBitSize_CPU(depth, maxDepth);

        uint32_t heapIndex = bitOffset >> 5;
        uint32_t localBitOffset = bitOffset & 31u;

        uint32_t bitCountLSB = std::min(32u - localBitOffset, (uint32_t)bitCount);
        uint32_t bitCountMSB = (uint32_t)bitCount - bitCountLSB;

        uint32_t maskLSB = (1u << bitCountLSB) - 1u;
        uint32_t lsb = (heap[heapIndex] >> localBitOffset) & maskLSB;

        uint32_t msb = 0;
        if (bitCountMSB > 0 && heapIndex + 1 < heap.size()) {
            uint32_t maskMSB = (1u << bitCountMSB) - 1u;
            msb = heap[heapIndex + 1] & maskMSB;
        }

        return lsb | (msb << bitCountLSB);
    }

    // Write a value to the heap at a specific node position
    void cbt_HeapWrite_CPU(std::vector<uint32_t>& heap, uint32_t id, int depth, int maxDepth, uint32_t value) {
        uint32_t bitOffset = cbt_NodeBitID_CPU(id, depth, maxDepth);
        int bitCount = cbt_NodeBitSize_CPU(depth, maxDepth);

        uint32_t heapIndex = bitOffset >> 5;
        uint32_t localBitOffset = bitOffset & 31u;

        uint32_t bitCountLSB = std::min(32u - localBitOffset, (uint32_t)bitCount);
        uint32_t bitCountMSB = (uint32_t)bitCount - bitCountLSB;

        // Clear and set LSB part
        uint32_t maskLSB = ~(((1u << bitCountLSB) - 1u) << localBitOffset);
        heap[heapIndex] = (heap[heapIndex] & maskLSB) | ((value & ((1u << bitCountLSB) - 1u)) << localBitOffset);

        // If value spans two words, write MSB part
        if (bitCountMSB > 0 && heapIndex + 1 < heap.size()) {
            uint32_t maskMSB = ~((1u << bitCountMSB) - 1u);
            heap[heapIndex + 1] = (heap[heapIndex + 1] & maskMSB) | (value >> bitCountLSB);
        }
    }

    // Compute sum reduction from leaf nodes up to root
    void cbt_ComputeSumReduction_CPU(std::vector<uint32_t>& heap, int maxDepth, int leafDepth) {
        // Start from leafDepth-1 and work up to depth 0
        for (int depth = leafDepth - 1; depth >= 0; --depth) {
            uint32_t minNodeID = 1u << depth;
            uint32_t maxNodeID = 2u << depth;

            for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
                // Left child = nodeID * 2, Right child = nodeID * 2 + 1
                uint32_t leftChild = nodeID << 1;
                uint32_t rightChild = leftChild | 1u;

                uint32_t leftValue = cbt_HeapRead_CPU(heap, leftChild, depth + 1, maxDepth);
                uint32_t rightValue = cbt_HeapRead_CPU(heap, rightChild, depth + 1, maxDepth);

                cbt_HeapWrite_CPU(heap, nodeID, depth, maxDepth, leftValue + rightValue);
            }
        }
    }
}

bool TerrainSystem::initializeCBT() {
    // Initialize CBT with triangles covering the terrain quad
    // Following libcbt's cbt_ResetToDepth pattern

    std::vector<uint32_t> initData(cbtBufferSize / sizeof(uint32_t), 0);
    int maxDepth = config.maxDepth;
    int initDepth = 6;  // Start with more triangles for initial coverage (2^6 = 64 triangles)

    // heap[0] stores (1 << maxDepth) as a marker for the max depth
    initData[0] = (1u << maxDepth);

    // Step 1: Set bitfield bits for all leaf nodes at initDepth
    // At depth 1, we have nodes with IDs 2 and 3
    uint32_t minNodeID = 1u << initDepth;  // 2
    uint32_t maxNodeID = 2u << initDepth;  // 4

    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cbt_HeapWrite_BitField_CPU(initData, nodeID, initDepth, maxDepth);
    }

    // Step 2: Initialize leaf node counts to 1
    for (uint32_t nodeID = minNodeID; nodeID < maxNodeID; ++nodeID) {
        cbt_HeapWrite_CPU(initData, nodeID, initDepth, maxDepth, 1);
    }

    // Step 3: Compute sum reduction upward from initDepth to depth 0
    cbt_ComputeSumReduction_CPU(initData, maxDepth, initDepth);

    // Verify: root should have count = 2
    uint32_t rootCount = cbt_HeapRead_CPU(initData, 1, 0, maxDepth);
    std::cout << "CBT root count after init: " << rootCount << std::endl;

    // Map the CBT buffer and write initialization data
    void* data;
    if (vmaMapMemory(allocator, cbtAllocation, &data) != VK_SUCCESS) {
        std::cerr << "Failed to map CBT buffer for initialization" << std::endl;
        return false;
    }
    memcpy(data, initData.data(), cbtBufferSize);
    vmaUnmapMemory(allocator, cbtAllocation);

    std::cout << "CBT initialized with " << (maxNodeID - minNodeID) << " triangles at depth " << initDepth << ", max depth " << maxDepth << std::endl;

    return true;
}

bool TerrainSystem::createHeightMap() {
    // Generate a procedural height map
    cpuHeightMap.resize(heightMapResolution * heightMapResolution);

    // Generate simple procedural terrain using multiple octaves of noise
    for (uint32_t y = 0; y < heightMapResolution; y++) {
        for (uint32_t x = 0; x < heightMapResolution; x++) {
            float fx = static_cast<float>(x) / heightMapResolution;
            float fy = static_cast<float>(y) / heightMapResolution;

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

            // Flatten center area where scene objects are (dist < 0.08 = ~40 world units from center)
            float flattenFactor = glm::smoothstep(0.02f, 0.08f, dist);
            height *= flattenFactor;

            // Normalize to [0, 1]
            height = (height + 1.0f) * 0.5f;
            cpuHeightMap[y * heightMapResolution + x] = height;
        }
    }

    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = {heightMapResolution, heightMapResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &heightMapImage, &heightMapAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = heightMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &heightMapView) != VK_SUCCESS) {
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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &heightMapSampler) != VK_SUCCESS) {
        return false;
    }

    // Upload height map data to GPU
    if (!uploadImageData(heightMapImage, cpuHeightMap.data(), heightMapResolution, heightMapResolution,
                         VK_FORMAT_R32_SFLOAT, sizeof(float))) {
        std::cerr << "Failed to upload height map to GPU" << std::endl;
        return false;
    }

    std::cout << "Height map uploaded: " << heightMapResolution << "x" << heightMapResolution << std::endl;
    return true;
}

bool TerrainSystem::createTerrainTexture() {
    // Create a simple procedural grass texture
    const uint32_t texSize = 256;
    std::vector<uint8_t> texData(texSize * texSize * 4);

    for (uint32_t y = 0; y < texSize; y++) {
        for (uint32_t x = 0; x < texSize; x++) {
            float fx = static_cast<float>(x) / texSize;
            float fy = static_cast<float>(y) / texSize;

            // Green grass base color with variation
            float noise = 0.5f + 0.5f * sin(fx * 50.0f) * sin(fy * 50.0f);
            noise += 0.25f * sin(fx * 100.0f + 1.0f) * sin(fy * 100.0f + 0.5f);

            uint8_t r = static_cast<uint8_t>(std::clamp((80 + 40 * noise), 0.0f, 255.0f));
            uint8_t g = static_cast<uint8_t>(std::clamp((120 + 60 * noise), 0.0f, 255.0f));
            uint8_t b = static_cast<uint8_t>(std::clamp((60 + 30 * noise), 0.0f, 255.0f));

            size_t idx = (y * texSize + x) * 4;
            texData[idx + 0] = r;
            texData[idx + 1] = g;
            texData[idx + 2] = b;
            texData[idx + 3] = 255;
        }
    }

    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {texSize, texSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &terrainAlbedoImage, &terrainAlbedoAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = terrainAlbedoImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &terrainAlbedoView) != VK_SUCCESS) {
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &terrainAlbedoSampler) != VK_SUCCESS) {
        return false;
    }

    // Upload terrain albedo texture to GPU
    if (!uploadImageData(terrainAlbedoImage, texData.data(), texSize, texSize,
                         VK_FORMAT_R8G8B8A8_SRGB, 4)) {
        std::cerr << "Failed to upload terrain albedo texture to GPU" << std::endl;
        return false;
    }

    std::cout << "Terrain albedo texture uploaded: " << texSize << "x" << texSize << std::endl;

    // === Create Grass Far LOD Texture ===
    // This texture represents how grass looks from a distance (blended into terrain)
    const uint32_t grassTexSize = 256;
    std::vector<uint8_t> grassTexData(grassTexSize * grassTexSize * 4);

    for (uint32_t y = 0; y < grassTexSize; y++) {
        for (uint32_t x = 0; x < grassTexSize; x++) {
            float fx = static_cast<float>(x) / grassTexSize;
            float fy = static_cast<float>(y) / grassTexSize;

            // Grass-like color with subtle variation (matches grass blade colors)
            // Base grass color (darker green)
            float noise = 0.5f + 0.3f * sin(fx * 80.0f) * sin(fy * 80.0f);
            noise += 0.15f * sin(fx * 160.0f + 0.5f) * sin(fy * 160.0f + 0.3f);

            // Match grass blade colors from grass.vert (baseColor to tipColor blend)
            // baseColor = vec3(0.08, 0.22, 0.04), tipColor = vec3(0.35, 0.65, 0.18)
            // Average grass field appearance
            uint8_t r = static_cast<uint8_t>(std::clamp((60 + 30 * noise), 0.0f, 255.0f));
            uint8_t g = static_cast<uint8_t>(std::clamp((130 + 50 * noise), 0.0f, 255.0f));
            uint8_t b = static_cast<uint8_t>(std::clamp((40 + 25 * noise), 0.0f, 255.0f));

            size_t idx = (y * grassTexSize + x) * 4;
            grassTexData[idx + 0] = r;
            grassTexData[idx + 1] = g;
            grassTexData[idx + 2] = b;
            grassTexData[idx + 3] = 255;
        }
    }

    // Create Vulkan image for grass far LOD
    VkImageCreateInfo grassImageInfo{};
    grassImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    grassImageInfo.imageType = VK_IMAGE_TYPE_2D;
    grassImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    grassImageInfo.extent = {grassTexSize, grassTexSize, 1};
    grassImageInfo.mipLevels = 1;
    grassImageInfo.arrayLayers = 1;
    grassImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    grassImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    grassImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    grassImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    grassImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo grassAllocInfo{};
    grassAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &grassImageInfo, &grassAllocInfo, &grassFarLODImage, &grassFarLODAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view for grass far LOD
    VkImageViewCreateInfo grassViewInfo{};
    grassViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    grassViewInfo.image = grassFarLODImage;
    grassViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    grassViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    grassViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    grassViewInfo.subresourceRange.baseMipLevel = 0;
    grassViewInfo.subresourceRange.levelCount = 1;
    grassViewInfo.subresourceRange.baseArrayLayer = 0;
    grassViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &grassViewInfo, nullptr, &grassFarLODView) != VK_SUCCESS) {
        return false;
    }

    // Create sampler for grass far LOD (same as terrain albedo)
    VkSamplerCreateInfo grassSamplerInfo{};
    grassSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    grassSamplerInfo.magFilter = VK_FILTER_LINEAR;
    grassSamplerInfo.minFilter = VK_FILTER_LINEAR;
    grassSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    grassSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    grassSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    grassSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    grassSamplerInfo.anisotropyEnable = VK_TRUE;
    grassSamplerInfo.maxAnisotropy = 16.0f;

    if (vkCreateSampler(device, &grassSamplerInfo, nullptr, &grassFarLODSampler) != VK_SUCCESS) {
        return false;
    }

    // Upload grass far LOD texture to GPU
    if (!uploadImageData(grassFarLODImage, grassTexData.data(), grassTexSize, grassTexSize,
                         VK_FORMAT_R8G8B8A8_SRGB, 4)) {
        std::cerr << "Failed to upload grass far LOD texture to GPU" << std::endl;
        return false;
    }

    std::cout << "Grass far LOD texture uploaded: " << grassTexSize << "x" << grassTexSize << std::endl;
    return true;
}

bool TerrainSystem::createUniformBuffers() {
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(TerrainUniforms);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                           &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool TerrainSystem::createIndirectBuffers() {
    // Indirect dispatch buffer (3 uints: x, y, z)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 3;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDispatchBuffer,
                           &indirectDispatchAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // Indirect draw buffer (4 uints: vertexCount, instanceCount, firstVertex, firstInstance)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uint32_t) * 4;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDrawBuffer,
                           &indirectDrawAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        // Initialize with default values (2 triangles = 6 vertices)
        uint32_t drawArgs[4] = {6, 1, 0, 0};  // vertexCount, instanceCount, firstVertex, firstInstance
        void* mapped;
        vmaMapMemory(allocator, indirectDrawAllocation, &mapped);
        memcpy(mapped, drawArgs, sizeof(drawArgs));
        vmaUnmapMemory(allocator, indirectDrawAllocation);
    }

    return true;
}

bool TerrainSystem::createComputeDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
    };

    std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
        makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        makeComputeBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TerrainSystem::createRenderDescriptorSetLayout() {
    auto makeGraphicsBinding = [](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags) {
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(stageFlags)
            .build();
    };

    std::array<VkDescriptorSetLayoutBinding, 8> bindings = {
        makeGraphicsBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
        makeGraphicsBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        makeGraphicsBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &renderDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TerrainSystem::createDescriptorSets() {
    // Allocate compute descriptor sets
    computeDescriptorSets.resize(framesInFlight);
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, computeDescriptorSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    // Allocate render descriptor sets
    renderDescriptorSets.resize(framesInFlight);
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, renderDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, renderDescriptorSets.data()) != VK_SUCCESS) {
            return false;
        }
    }

    // Update compute descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 5> writes{};

        VkDescriptorBufferInfo cbtInfo{cbtBuffer, 0, cbtBufferSize};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cbtInfo;

        VkDescriptorBufferInfo dispatchInfo{indirectDispatchBuffer, 0, sizeof(uint32_t) * 3};
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &dispatchInfo;

        VkDescriptorBufferInfo drawInfo{indirectDrawBuffer, 0, sizeof(uint32_t) * 4};
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = computeDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &drawInfo;

        VkDescriptorImageInfo heightMapInfo{heightMapSampler, heightMapView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = computeDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &heightMapInfo;

        VkDescriptorBufferInfo uniformInfo{uniformBuffers[i], 0, sizeof(TerrainUniforms)};
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = computeDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &uniformInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

bool TerrainSystem::createDispatcherPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_dispatcher.comp.spv");
    if (!shaderModule) return false;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainDispatcherPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &dispatcherPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = dispatcherPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &dispatcherPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createSubdivisionPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_subdivision.comp.spv");
    if (!shaderModule) return false;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &subdivisionPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = subdivisionPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &subdivisionPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createSumReductionPipelines() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainSumReductionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sumReductionPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Prepass pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction_prepass.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPrepassPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    // Regular sum reduction pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/terrain/terrain_sum_reduction.comp.spv");
        if (!shaderModule) return false;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = sumReductionPipelineLayout;

        VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sumReductionPipeline);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        if (result != VK_SUCCESS) return false;
    }

    return true;
}

bool TerrainSystem::createRenderPipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // No vertex input - vertices generated procedurally
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = renderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderPipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createWireframePipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_wireframe.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;  // Wireframe
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for wireframe

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = renderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
}

bool TerrainSystem::createShadowPipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/terrain/terrain_shadow.frag.spv");
    if (!vertModule || !fragModule) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front-face culling for shadow maps
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;  // No color attachments for shadow pass

    std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainShadowPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return result == VK_SUCCESS;
}

void TerrainSystem::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Extract frustum planes from view-projection matrix
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );
    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
}

bool TerrainSystem::uploadImageData(VkImage image, const void* data, uint32_t width, uint32_t height,
                                     VkFormat format, uint32_t bytesPerPixel) {
    VkDeviceSize imageSize = width * height * bytesPerPixel;

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
        std::cerr << "Failed to create staging buffer for image upload" << std::endl;
        return false;
    }

    // Copy data to staging buffer
    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, data, imageSize);
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
    region.imageExtent = {width, height, 1};

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

void TerrainSystem::updateDescriptorSets(VkDevice device,
                                          const std::vector<VkBuffer>& sceneUniformBuffers,
                                          VkImageView shadowMapView,
                                          VkSampler shadowSampler) {
    // Update render descriptor sets with scene resources
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::vector<VkWriteDescriptorSet> writes;

        // CBT buffer
        VkDescriptorBufferInfo cbtInfo{cbtBuffer, 0, cbtBufferSize};
        VkWriteDescriptorSet cbtWrite{};
        cbtWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cbtWrite.dstSet = renderDescriptorSets[i];
        cbtWrite.dstBinding = 0;
        cbtWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cbtWrite.descriptorCount = 1;
        cbtWrite.pBufferInfo = &cbtInfo;
        writes.push_back(cbtWrite);

        // Height map
        VkDescriptorImageInfo heightMapInfo{heightMapSampler, heightMapView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet heightMapWrite{};
        heightMapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        heightMapWrite.dstSet = renderDescriptorSets[i];
        heightMapWrite.dstBinding = 3;
        heightMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        heightMapWrite.descriptorCount = 1;
        heightMapWrite.pImageInfo = &heightMapInfo;
        writes.push_back(heightMapWrite);

        // Terrain uniforms
        VkDescriptorBufferInfo uniformInfo{uniformBuffers[i], 0, sizeof(TerrainUniforms)};
        VkWriteDescriptorSet uniformWrite{};
        uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWrite.dstSet = renderDescriptorSets[i];
        uniformWrite.dstBinding = 4;
        uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.descriptorCount = 1;
        uniformWrite.pBufferInfo = &uniformInfo;
        writes.push_back(uniformWrite);

        // Scene UBO
        if (i < sceneUniformBuffers.size()) {
            VkDescriptorBufferInfo sceneInfo{sceneUniformBuffers[i], 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet sceneWrite{};
            sceneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            sceneWrite.dstSet = renderDescriptorSets[i];
            sceneWrite.dstBinding = 5;
            sceneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            sceneWrite.descriptorCount = 1;
            sceneWrite.pBufferInfo = &sceneInfo;
            writes.push_back(sceneWrite);
        }

        // Terrain albedo
        VkDescriptorImageInfo albedoInfo{terrainAlbedoSampler, terrainAlbedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet albedoWrite{};
        albedoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        albedoWrite.dstSet = renderDescriptorSets[i];
        albedoWrite.dstBinding = 6;
        albedoWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        albedoWrite.descriptorCount = 1;
        albedoWrite.pImageInfo = &albedoInfo;
        writes.push_back(albedoWrite);

        // Shadow map
        if (shadowMapView != VK_NULL_HANDLE) {
            VkDescriptorImageInfo shadowInfo{shadowSampler, shadowMapView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet shadowWrite{};
            shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            shadowWrite.dstSet = renderDescriptorSets[i];
            shadowWrite.dstBinding = 7;
            shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            shadowWrite.descriptorCount = 1;
            shadowWrite.pImageInfo = &shadowInfo;
            writes.push_back(shadowWrite);
        }

        // Grass far LOD texture
        if (grassFarLODView != VK_NULL_HANDLE) {
            VkDescriptorImageInfo grassFarLODInfo{grassFarLODSampler, grassFarLODView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet grassFarLODWrite{};
            grassFarLODWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            grassFarLODWrite.dstSet = renderDescriptorSets[i];
            grassFarLODWrite.dstBinding = 8;
            grassFarLODWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            grassFarLODWrite.descriptorCount = 1;
            grassFarLODWrite.pImageInfo = &grassFarLODInfo;
            writes.push_back(grassFarLODWrite);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void TerrainSystem::setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler) {
    // Update render descriptor sets with snow mask texture
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkDescriptorImageInfo snowMaskInfo{snowMaskSampler, snowMaskView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet snowMaskWrite{};
        snowMaskWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        snowMaskWrite.dstSet = renderDescriptorSets[i];
        snowMaskWrite.dstBinding = 9;
        snowMaskWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        snowMaskWrite.descriptorCount = 1;
        snowMaskWrite.pImageInfo = &snowMaskInfo;

        vkUpdateDescriptorSets(device, 1, &snowMaskWrite, 0, nullptr);
    }
}

void TerrainSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                    const glm::mat4& view, const glm::mat4& proj) {
    TerrainUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    uniforms.terrainParams = glm::vec4(
        config.size,
        config.heightScale,
        config.targetEdgePixels,
        static_cast<float>(config.maxDepth)
    );

    uniforms.lodParams = glm::vec4(
        config.splitThreshold,
        config.mergeThreshold,
        static_cast<float>(config.minDepth),
        0.0f
    );

    uniforms.screenSize = glm::vec2(extent.width, extent.height);

    // Compute LOD factor for screen-space edge length calculation
    float fov = 2.0f * atan(1.0f / proj[1][1]);
    uniforms.lodFactor = 2.0f * log2(extent.height / (2.0f * tan(fov * 0.5f) * config.targetEdgePixels));

    // Extract frustum planes
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(TerrainUniforms));
}

void TerrainSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Memory barrier before compute
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    // 1. Dispatcher - set up indirect args
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainDispatcherPushConstants dispatcherPC{};
    dispatcherPC.subdivisionWorkgroupSize = SUBDIVISION_WORKGROUP_SIZE;
    dispatcherPC.meshletVertexCount = 0;  // Direct triangle rendering
    vkCmdPushConstants(cmd, dispatcherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);

    vkCmdDispatch(cmd, 1, 1, 1);

    // Barrier after dispatcher
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 2. Subdivision - LOD update (indirect dispatch)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);
    vkCmdDispatchIndirect(cmd, indirectDispatchBuffer, 0);

    // Barrier after subdivision
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 3. Sum reduction - rebuild the sum tree
    // First prepass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPrepassPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    TerrainSumReductionPushConstants sumPC{};
    sumPC.passID = config.maxDepth;
    vkCmdPushConstants(cmd, sumReductionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(sumPC), &sumPC);

    uint32_t workgroups = std::max(1u, (1u << (config.maxDepth - 5)) / SUM_REDUCTION_WORKGROUP_SIZE);
    vkCmdDispatch(cmd, workgroups, 1, 1);

    // Barrier
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Subsequent reduction passes
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sumReductionPipeline);

    for (int pass = config.maxDepth - 6; pass >= 0; pass--) {
        sumPC.passID = pass;
        vkCmdPushConstants(cmd, sumReductionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(sumPC), &sumPC);

        workgroups = std::max(1u, (1u << pass) / SUM_REDUCTION_WORKGROUP_SIZE);
        vkCmdDispatch(cmd, std::max(1u, workgroups), 1, 1);

        // Barrier between passes
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    // 4. Final dispatcher pass to update draw args
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdPushConstants(cmd, dispatcherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                      sizeof(dispatcherPC), &dispatcherPC);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Final barrier before rendering
    VkMemoryBarrier renderBarrier{};
    renderBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    renderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    renderBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        0, 1, &renderBarrier, 0, nullptr, 0, nullptr);
}

void TerrainSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Bind pipeline
    VkPipeline pipeline = wireframeMode ? wireframePipeline : renderPipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout,
                           0, 1, &renderDescriptorSets[frameIndex], 0, nullptr);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Indirect draw using CBT node count
    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void TerrainSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::mat4& lightViewProj, int cascadeIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout,
                           0, 1, &renderDescriptorSets[frameIndex], 0, nullptr);

    // Set viewport and scissor for shadow map
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Depth bias for shadow acne prevention
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

    // Push constants
    TerrainShadowPushConstants pc{};
    pc.lightViewProj = lightViewProj;
    pc.terrainSize = config.size;
    pc.heightScale = config.heightScale;
    pc.cascadeIndex = cascadeIndex;
    vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    // Indirect draw
    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

float TerrainSystem::getHeightAt(float x, float z) const {
    // Convert world position to UV coordinates
    float u = (x / config.size) + 0.5f;
    float v = (z / config.size) + 0.5f;

    // Clamp to valid range
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Sample height map with bilinear interpolation
    float fx = u * (heightMapResolution - 1);
    float fy = v * (heightMapResolution - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(heightMapResolution - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(heightMapResolution - 1));

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = cpuHeightMap[y0 * heightMapResolution + x0];
    float h10 = cpuHeightMap[y0 * heightMapResolution + x1];
    float h01 = cpuHeightMap[y1 * heightMapResolution + x0];
    float h11 = cpuHeightMap[y1 * heightMapResolution + x1];

    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;
    float h = h0 * (1.0f - ty) + h1 * ty;

    // Match shader: (h - 0.5) * heightScale, centers height around Y=0
    return (h - 0.5f) * config.heightScale;
}
