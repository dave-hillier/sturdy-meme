#include "TerrainCBT.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <stb_image.h>

bool TerrainCBT::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    commandPool = info.commandPool;
    graphicsQueue = info.graphicsQueue;

    if (!createCBTBuffer()) {
        SDL_Log("Failed to create CBT buffer");
        return false;
    }

    if (!initializeCBT()) {
        SDL_Log("Failed to initialize CBT");
        return false;
    }

    if (!createIndirectBuffers()) {
        SDL_Log("Failed to create indirect buffers");
        return false;
    }

    if (!createHeightMapResources()) {
        SDL_Log("Failed to create height map resources");
        return false;
    }

    if (!createDescriptorSetLayouts()) {
        SDL_Log("Failed to create descriptor set layouts");
        return false;
    }

    if (!createComputePipelines()) {
        SDL_Log("Failed to create compute pipelines");
        return false;
    }

    if (!createGraphicsPipeline()) {
        SDL_Log("Failed to create graphics pipeline");
        return false;
    }

    if (!createShadowPipeline()) {
        SDL_Log("Failed to create shadow pipeline");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_Log("Failed to create descriptor sets");
        return false;
    }

    SDL_Log("TerrainCBT initialized successfully (maxDepth=%u, terrainSize=%.1f)", maxDepth, terrainSize);
    return true;
}

void TerrainCBT::destroy() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy pipelines
    if (dispatcherPipeline) vkDestroyPipeline(device, dispatcherPipeline, nullptr);
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (sumReductionPipeline) vkDestroyPipeline(device, sumReductionPipeline, nullptr);
    if (graphicsPipeline) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    if (shadowPipeline) vkDestroyPipeline(device, shadowPipeline, nullptr);

    // Destroy pipeline layouts
    if (computePipelineLayout) vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    if (graphicsPipelineLayout) vkDestroyPipelineLayout(device, graphicsPipelineLayout, nullptr);
    if (shadowPipelineLayout) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (graphicsDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, graphicsDescriptorSetLayout, nullptr);
    if (shadowDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, shadowDescriptorSetLayout, nullptr);

    // Destroy CBT buffer
    if (cbtBuffer) vmaDestroyBuffer(allocator, cbtBuffer, cbtAllocation);

    // Destroy indirect buffers
    for (size_t i = 0; i < indirectDispatchBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, indirectDispatchBuffers[i], indirectDispatchAllocations[i]);
    }
    for (size_t i = 0; i < indirectDrawBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, indirectDrawBuffers[i], indirectDrawAllocations[i]);
    }

    // Destroy height map resources
    if (heightMapSampler) vkDestroySampler(device, heightMapSampler, nullptr);
    if (heightMapView) vkDestroyImageView(device, heightMapView, nullptr);
    if (heightMapImage) vmaDestroyImage(allocator, heightMapImage, heightMapAllocation);

    SDL_Log("TerrainCBT destroyed");
}

bool TerrainCBT::createCBTBuffer() {
    // Calculate buffer size
    // CBT layout: [sum reduction tree] [bitfield]
    // Bitfield size: 2^(maxDepth) bits = 2^(maxDepth-3) bytes
    // Sum reduction: ~maxDepth levels of progressively smaller arrays

    // For maxDepth = 10:
    // Bitfield: 2^10 = 1024 bits = 128 bytes = 32 uints
    // Sum reduction: header + levels

    // Simplified layout:
    // [0]: leaf count (root of sum tree)
    // [1-15]: reserved/sum tree intermediate
    // [16+]: bitfield

    uint64_t bitfieldBits = 1ull << maxDepth;
    uint64_t bitfieldUints = (bitfieldBits + 31) / 32;
    uint64_t headerUints = 16;  // Reserved for sum reduction tree

    cbtBufferSize = (headerUints + bitfieldUints) * sizeof(uint32_t);

    // Align to 256 bytes (typical minStorageBufferOffsetAlignment)
    cbtBufferSize = ((cbtBufferSize + 255) / 256) * 256;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = cbtBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &cbtBuffer, &cbtAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create CBT buffer");
        return false;
    }

    SDL_Log("CBT buffer created: %llu bytes (maxDepth=%u)", (unsigned long long)cbtBufferSize, maxDepth);
    return true;
}

bool TerrainCBT::initializeCBT() {
    // Initialize CBT with two base triangles
    // Header: [leafCount=2, reserved...]
    // Bitfield: bits 0 and 1 set (heap indices 1 and 2)

    uint64_t bitfieldUints = (1ull << maxDepth) / 32;
    uint64_t totalUints = 16 + bitfieldUints;

    std::vector<uint32_t> initData(totalUints, 0);

    // Set leaf count
    initData[0] = 2;

    // Set bits for base triangles (heap indices 1 and 2)
    // Bit index = heapIndex - 1
    // Heap index 1 -> bit 0 -> word 0, bit 0
    // Heap index 2 -> bit 1 -> word 0, bit 1
    initData[16] = 0x3;  // bits 0 and 1 set

    uploadBufferData(cbtBuffer, initData.data(), initData.size() * sizeof(uint32_t));

    return true;
}

void TerrainCBT::uploadBufferData(VkBuffer buffer, const void* data, VkDeviceSize size) {
    // Create staging buffer
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo allocInfo;

    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                    &stagingBuffer, &stagingAllocation, &allocInfo);

    memcpy(allocInfo.pMappedData, data, size);

    // Copy to device buffer
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

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer, buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

bool TerrainCBT::createIndirectBuffers() {
    indirectDispatchBuffers.resize(framesInFlight);
    indirectDispatchAllocations.resize(framesInFlight);
    indirectDrawBuffers.resize(framesInFlight);
    indirectDrawAllocations.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Indirect dispatch buffer (3 uints: x, y, z)
        VkBufferCreateInfo dispatchInfo{};
        dispatchInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dispatchInfo.size = 3 * sizeof(uint32_t);
        dispatchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        dispatchInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &dispatchInfo, &allocInfo,
                            &indirectDispatchBuffers[i], &indirectDispatchAllocations[i],
                            nullptr) != VK_SUCCESS) {
            return false;
        }

        // Initialize with 1 workgroup
        uint32_t initDispatch[3] = {1, 1, 1};
        uploadBufferData(indirectDispatchBuffers[i], initDispatch, sizeof(initDispatch));

        // Indirect draw buffer (4 uints: vertexCount, instanceCount, firstVertex, firstInstance)
        VkBufferCreateInfo drawInfo{};
        drawInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        drawInfo.size = 4 * sizeof(uint32_t);
        drawInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        drawInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(allocator, &drawInfo, &allocInfo,
                            &indirectDrawBuffers[i], &indirectDrawAllocations[i],
                            nullptr) != VK_SUCCESS) {
            return false;
        }

        // Initialize with 2 triangles * 3 vertices
        uint32_t initDraw[4] = {6, 1, 0, 0};
        uploadBufferData(indirectDrawBuffers[i], initDraw, sizeof(initDraw));
    }

    return true;
}

bool TerrainCBT::createHeightMapResources() {
    // Create a default flat height map
    std::vector<uint8_t> flatData(DEFAULT_HEIGHTMAP_SIZE * DEFAULT_HEIGHTMAP_SIZE, 128);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = DEFAULT_HEIGHTMAP_SIZE;
    imageInfo.extent.height = DEFAULT_HEIGHTMAP_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &heightMapImage, &heightMapAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create staging buffer and upload
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = flatData.size();
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingInfo2;

    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                    &stagingBuffer, &stagingAlloc, &stagingInfo2);
    memcpy(stagingInfo2.pMappedData, flatData.data(), flatData.size());

    // Transition and copy
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
    barrier.image = heightMapImage;
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

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {DEFAULT_HEIGHTMAP_SIZE, DEFAULT_HEIGHTMAP_SIZE, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, heightMapImage,
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
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = heightMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
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
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &heightMapSampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TerrainCBT::loadHeightMap(const std::string& path) {
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 1);

    if (!pixels) {
        SDL_Log("Failed to load height map: %s", path.c_str());
        return false;
    }

    // Destroy old resources
    if (heightMapView) {
        vkDestroyImageView(device, heightMapView, nullptr);
        heightMapView = VK_NULL_HANDLE;
    }
    if (heightMapImage) {
        vmaDestroyImage(allocator, heightMapImage, heightMapAllocation);
        heightMapImage = VK_NULL_HANDLE;
    }

    // Create new image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &heightMapImage, &heightMapAllocation, nullptr) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return false;
    }

    // Upload image data (similar to createHeightMapResources but with actual data)
    VkDeviceSize imageSize = width * height;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingInfo2;

    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                    &stagingBuffer, &stagingAlloc, &stagingInfo2);
    memcpy(stagingInfo2.pMappedData, pixels, imageSize);
    stbi_image_free(pixels);

    // Transition and copy
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

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = heightMapImage;
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

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, heightMapImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = heightMapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &heightMapView) != VK_SUCCESS) {
        return false;
    }

    SDL_Log("Loaded height map: %s (%dx%d)", path.c_str(), width, height);
    return true;
}

bool TerrainCBT::createDescriptorSetLayouts() {
    // Compute descriptor set layout
    // binding 0: CBT buffer (storage)
    // binding 1: Indirect dispatch buffer (storage)
    // binding 2: Indirect draw buffer (storage)
    // binding 3: Height map (sampler)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                        &computeDescriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
    }

    // Graphics descriptor set layout
    // binding 0: UBO (uniform)
    // binding 1: Albedo texture (sampler)
    // binding 2: Shadow map array (sampler)
    // binding 3: Normal map (sampler)
    // binding 4: CBT buffer (storage, read-only in vertex shader)
    // binding 5: Height map (sampler)
    {
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        bindings[5].binding = 5;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                        &graphicsDescriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
    }

    // Shadow descriptor set layout
    // binding 0: CBT buffer (storage, read-only)
    // binding 1: Height map (sampler)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                        &shadowDescriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool TerrainCBT::createComputePipelines() {
    // Push constant range for compute shaders
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CBTComputePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &computePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create dispatcher pipeline
    auto dispatcherCode = ShaderLoader::readFile(shaderPath + "/terrain_dispatcher.comp.spv");
    if (dispatcherCode.empty()) {
        SDL_Log("Failed to load terrain_dispatcher.comp.spv");
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = dispatcherCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(dispatcherCode.data());

    VkShaderModule dispatcherModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &dispatcherModule) != VK_SUCCESS) {
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = dispatcherModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = computePipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                 &dispatcherPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, dispatcherModule, nullptr);
        return false;
    }
    vkDestroyShaderModule(device, dispatcherModule, nullptr);

    // Create subdivision pipeline
    auto subdivisionCode = ShaderLoader::readFile(shaderPath + "/terrain_cbt.comp.spv");
    if (subdivisionCode.empty()) {
        SDL_Log("Failed to load terrain_cbt.comp.spv");
        return false;
    }

    moduleInfo.codeSize = subdivisionCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(subdivisionCode.data());

    VkShaderModule subdivisionModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &subdivisionModule) != VK_SUCCESS) {
        return false;
    }

    pipelineInfo.stage.module = subdivisionModule;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                 &subdivisionPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, subdivisionModule, nullptr);
        return false;
    }
    vkDestroyShaderModule(device, subdivisionModule, nullptr);

    // Create sum reduction pipeline
    auto sumReductionCode = ShaderLoader::readFile(shaderPath + "/terrain_sum_reduction.comp.spv");
    if (sumReductionCode.empty()) {
        SDL_Log("Failed to load terrain_sum_reduction.comp.spv");
        return false;
    }

    moduleInfo.codeSize = sumReductionCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(sumReductionCode.data());

    VkShaderModule sumReductionModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &sumReductionModule) != VK_SUCCESS) {
        return false;
    }

    pipelineInfo.stage.module = sumReductionModule;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                 &sumReductionPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, sumReductionModule, nullptr);
        return false;
    }
    vkDestroyShaderModule(device, sumReductionModule, nullptr);

    return true;
}

bool TerrainCBT::createGraphicsPipeline() {
    // Load shaders
    auto vertCode = ShaderLoader::readFile(shaderPath + "/terrain.vert.spv");
    auto fragCode = ShaderLoader::readFile(shaderPath + "/terrain.frag.spv");

    if (vertCode.empty() || fragCode.empty()) {
        SDL_Log("Failed to load terrain shaders");
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    moduleInfo.codeSize = vertCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    VkShaderModule vertModule;
    vkCreateShaderModule(device, &moduleInfo, nullptr, &vertModule);

    moduleInfo.codeSize = fragCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    VkShaderModule fragModule;
    vkCreateShaderModule(device, &moduleInfo, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
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
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for terrain
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &graphicsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &graphicsPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &graphicsPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return true;
}

bool TerrainCBT::createShadowPipeline() {
    auto vertCode = ShaderLoader::readFile(shaderPath + "/terrain_shadow.vert.spv");
    auto fragCode = ShaderLoader::readFile(shaderPath + "/terrain_shadow.frag.spv");

    if (vertCode.empty() || fragCode.empty()) {
        SDL_Log("Failed to load terrain shadow shaders");
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    moduleInfo.codeSize = vertCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    VkShaderModule vertModule;
    vkCreateShaderModule(device, &moduleInfo, nullptr, &vertModule);

    moduleInfo.codeSize = fragCode.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    VkShaderModule fragModule;
    vkCreateShaderModule(device, &moduleInfo, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
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

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_TRUE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front face culling for shadow map
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 1.5f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    // Push constants for shadow pass
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TerrainShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &shadowPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return true;
}

bool TerrainCBT::createDescriptorSets() {
    // Allocate compute descriptor sets
    computeDescriptorSets.resize(framesInFlight);
    std::vector<VkDescriptorSetLayout> computeLayouts(framesInFlight, computeDescriptorSetLayout);

    VkDescriptorSetAllocateInfo computeAllocInfo{};
    computeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeAllocInfo.descriptorPool = descriptorPool;
    computeAllocInfo.descriptorSetCount = framesInFlight;
    computeAllocInfo.pSetLayouts = computeLayouts.data();

    if (vkAllocateDescriptorSets(device, &computeAllocInfo, computeDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate compute descriptor sets");
        return false;
    }

    // Allocate graphics descriptor sets
    graphicsDescriptorSets.resize(framesInFlight);
    std::vector<VkDescriptorSetLayout> graphicsLayouts(framesInFlight, graphicsDescriptorSetLayout);

    VkDescriptorSetAllocateInfo graphicsAllocInfo{};
    graphicsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    graphicsAllocInfo.descriptorPool = descriptorPool;
    graphicsAllocInfo.descriptorSetCount = framesInFlight;
    graphicsAllocInfo.pSetLayouts = graphicsLayouts.data();

    if (vkAllocateDescriptorSets(device, &graphicsAllocInfo, graphicsDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate graphics descriptor sets");
        return false;
    }

    // Allocate shadow descriptor sets
    shadowDescriptorSets.resize(framesInFlight);
    std::vector<VkDescriptorSetLayout> shadowLayouts(framesInFlight, shadowDescriptorSetLayout);

    VkDescriptorSetAllocateInfo shadowAllocInfo{};
    shadowAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    shadowAllocInfo.descriptorPool = descriptorPool;
    shadowAllocInfo.descriptorSetCount = framesInFlight;
    shadowAllocInfo.pSetLayouts = shadowLayouts.data();

    if (vkAllocateDescriptorSets(device, &shadowAllocInfo, shadowDescriptorSets.data()) != VK_SUCCESS) {
        SDL_Log("Failed to allocate shadow descriptor sets");
        return false;
    }

    // Update compute descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 4> writes{};

        VkDescriptorBufferInfo cbtBufferInfo{};
        cbtBufferInfo.buffer = cbtBuffer;
        cbtBufferInfo.offset = 0;
        cbtBufferInfo.range = cbtBufferSize;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cbtBufferInfo;

        VkDescriptorBufferInfo dispatchBufferInfo{};
        dispatchBufferInfo.buffer = indirectDispatchBuffers[i];
        dispatchBufferInfo.offset = 0;
        dispatchBufferInfo.range = 3 * sizeof(uint32_t);

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &dispatchBufferInfo;

        VkDescriptorBufferInfo drawBufferInfo{};
        drawBufferInfo.buffer = indirectDrawBuffers[i];
        drawBufferInfo.offset = 0;
        drawBufferInfo.range = 4 * sizeof(uint32_t);

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = computeDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &drawBufferInfo;

        VkDescriptorImageInfo heightMapInfo{};
        heightMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        heightMapInfo.imageView = heightMapView;
        heightMapInfo.sampler = heightMapSampler;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = computeDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &heightMapInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Update shadow descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 2> writes{};

        VkDescriptorBufferInfo cbtBufferInfo{};
        cbtBufferInfo.buffer = cbtBuffer;
        cbtBufferInfo.offset = 0;
        cbtBufferInfo.range = cbtBufferSize;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = shadowDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cbtBufferInfo;

        VkDescriptorImageInfo heightMapInfo{};
        heightMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        heightMapInfo.imageView = heightMapView;
        heightMapInfo.sampler = heightMapSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = shadowDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &heightMapInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

void TerrainCBT::updateDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                       VkImageView shadowMapView, VkSampler shadowSampler) {
    // Update graphics descriptor sets with shared resources
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 6> writes{};

        // UBO
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = uniformBuffers[i];
        uboInfo.offset = 0;
        uboInfo.range = VK_WHOLE_SIZE;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = graphicsDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboInfo;

        // Albedo (use height map as placeholder - will be updated with actual texture)
        VkDescriptorImageInfo albedoInfo{};
        albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        albedoInfo.imageView = heightMapView;
        albedoInfo.sampler = heightMapSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = graphicsDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &albedoInfo;

        // Shadow map
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMapView;
        shadowInfo.sampler = shadowSampler;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = graphicsDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &shadowInfo;

        // Normal map (use height map as placeholder)
        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = heightMapView;
        normalInfo.sampler = heightMapSampler;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = graphicsDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &normalInfo;

        // CBT buffer
        VkDescriptorBufferInfo cbtInfo{};
        cbtInfo.buffer = cbtBuffer;
        cbtInfo.offset = 0;
        cbtInfo.range = cbtBufferSize;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = graphicsDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &cbtInfo;

        // Height map
        VkDescriptorImageInfo heightInfo{};
        heightInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        heightInfo.imageView = heightMapView;
        heightInfo.sampler = heightMapSampler;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = graphicsDescriptorSets[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].descriptorCount = 1;
        writes[5].pImageInfo = &heightInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void TerrainCBT::recordComputePass(VkCommandBuffer cmd, uint32_t frameIndex,
                                    const glm::mat4& viewProj, const glm::vec3& cameraPos,
                                    float screenWidth, float screenHeight) {
    // Run dispatcher to set indirect args
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dispatcherPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
                            0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    vkCmdDispatch(cmd, 1, 1, 1);

    // Barrier after dispatcher
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    // Skip subdivision for now - just use fixed 2-triangle base
    // This simplifies initial testing

    // Note: For full adaptive subdivision, we would:
    // 1. Run subdivision shader with indirect dispatch
    // 2. Run sum reduction to rebuild the tree
    // For Milestone 1, we start with a simpler static mesh
}

void TerrainCBT::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, bool wireframeDebug) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout,
                            0, 1, &graphicsDescriptorSets[frameIndex], 0, nullptr);

    TerrainPushConstants pushConstants{};
    pushConstants.terrainSize = terrainSize;
    pushConstants.heightScale = heightScale;
    pushConstants.maxDepth = static_cast<float>(maxDepth);
    pushConstants.debugWireframe = wireframeDebug ? 1.0f : 0.0f;

    vkCmdPushConstants(cmd, graphicsPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TerrainPushConstants), &pushConstants);

    // Draw with indirect buffer
    vkCmdDrawIndirect(cmd, indirectDrawBuffers[frameIndex], 0, 1, sizeof(VkDrawIndirectCommand));
}

void TerrainCBT::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                   const glm::mat4& lightViewProj, int cascadeIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout,
                            0, 1, &shadowDescriptorSets[frameIndex], 0, nullptr);

    TerrainShadowPushConstants pushConstants{};
    pushConstants.lightViewProj = lightViewProj;
    pushConstants.terrainSize = terrainSize;
    pushConstants.heightScale = heightScale;
    pushConstants.maxDepth = static_cast<float>(maxDepth);
    pushConstants.cascadeIndex = cascadeIndex;

    vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(TerrainShadowPushConstants), &pushConstants);

    // Draw with indirect buffer
    vkCmdDrawIndirect(cmd, indirectDrawBuffers[frameIndex], 0, 1, sizeof(VkDrawIndirectCommand));
}

uint32_t TerrainCBT::getLeafCount() const {
    return cachedLeafCount;
}
