#include "AtmosphereLUTSystem.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL_log.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool AtmosphereLUTSystem::exportImageToPNG(VkImage image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename) {
    // Determine channel count from format
    uint32_t channelCount = 0;
    switch (format) {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            channelCount = 4;
            break;
        case VK_FORMAT_R16G16_SFLOAT:
            channelCount = 2;
            break;
        case VK_FORMAT_R16_SFLOAT:
            channelCount = 1;
            break;
        default:
            SDL_Log("Unsupported format for PNG export: %d", format);
            return false;
    }

    // Create staging buffer with correct size based on actual format
    VkDeviceSize imageSize = width * height * channelCount * sizeof(uint16_t);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create staging buffer for PNG export");
        return false;
    }

    // Create command buffer for the copy
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assuming graphics queue family 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo2{};
    allocInfo2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo2.commandPool = commandPool;
    allocInfo2.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo2, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition image to TRANSFER_SRC_OPTIMAL
    Barriers::transitionImage(commandBuffer, image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    // Copy image to buffer
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

    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition back
    Barriers::transitionImage(commandBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Helper lambda to convert FP16 to float32
    auto fp16ToFloat = [](uint16_t h) -> float {
        uint32_t sign = (h & 0x8000) << 16;
        uint32_t exponent = (h & 0x7C00) >> 10;
        uint32_t mantissa = (h & 0x03FF);

        if (exponent == 0) {
            if (mantissa == 0) {
                // Zero
                uint32_t f = sign;
                return *reinterpret_cast<float*>(&f);
            } else {
                // Denormalized
                exponent = 1;
                while ((mantissa & 0x0400) == 0) {
                    mantissa <<= 1;
                    exponent--;
                }
                mantissa &= 0x03FF;
            }
        } else if (exponent == 0x1F) {
            // Infinity or NaN
            uint32_t f = sign | 0x7F800000 | (mantissa << 13);
            return *reinterpret_cast<float*>(&f);
        }

        // Normalized
        exponent = exponent + (127 - 15);
        mantissa = mantissa << 13;
        uint32_t f = sign | (exponent << 23) | mantissa;
        return *reinterpret_cast<float*>(&f);
    };

    // Map and convert to 8-bit RGBA for PNG
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);

    std::vector<uint8_t> rgba8(width * height * 4);
    uint16_t* src = static_cast<uint16_t*>(data);

    // Convert with proper FP16 decoding and per-format stride
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t srcIdx = (y * width + x) * channelCount;
            uint32_t dstIdx = (y * width + x) * 4;

            // Read available channels and convert from FP16 to float
            float channels[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Default: black with alpha=1
            for (uint32_t c = 0; c < channelCount; c++) {
                channels[c] = fp16ToFloat(src[srcIdx + c]);
            }

            // For RG formats, copy R to all RGB channels for grayscale visualization
            if (channelCount == 2) {
                // Use R for luminance, G for alpha (common for multi-scatter LUT)
                channels[2] = channels[0]; // B = R
                channels[3] = channels[1]; // A = G
                channels[1] = channels[0]; // G = R
            } else if (channelCount == 1) {
                channels[1] = channels[0];
                channels[2] = channels[0];
                channels[3] = 1.0f;
            }

            // Simple tonemapping: clamp to [0,1] and convert to 8-bit
            // No arbitrary scaling - the LUT values are already in the correct range
            for (uint32_t c = 0; c < 4; c++) {
                float val = glm::clamp(channels[c], 0.0f, 1.0f);
                rgba8[dstIdx + c] = static_cast<uint8_t>(val * 255.0f);
            }
        }
    }

    vmaUnmapMemory(allocator, stagingAllocation);

    // Write PNG
    int result = stbi_write_png(filename.c_str(), width, height, 4, rgba8.data(), width * 4);

    // Cleanup
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    vkDestroyCommandPool(device, commandPool, nullptr);

    if (result == 0) {
        SDL_Log("Failed to write PNG: %s", filename.c_str());
        return false;
    }

    SDL_Log("Exported LUT to: %s (%d channels)", filename.c_str(), channelCount);
    return true;
}

bool AtmosphereLUTSystem::exportLUTsAsPNG(const std::string& outputDir) {
    SDL_Log("Exporting atmosphere LUTs as PNG...");

    bool success = true;
    success &= exportImageToPNG(transmittanceLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT,
                                outputDir + "/transmittance_lut.png");

    success &= exportImageToPNG(multiScatterLUT, VK_FORMAT_R16G16_SFLOAT,
                                MULTISCATTER_SIZE, MULTISCATTER_SIZE,
                                outputDir + "/multiscatter_lut.png");

    success &= exportImageToPNG(skyViewLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                SKYVIEW_WIDTH, SKYVIEW_HEIGHT,
                                outputDir + "/skyview_lut.png");

    success &= exportImageToPNG(cloudMapLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                CLOUDMAP_SIZE, CLOUDMAP_SIZE,
                                outputDir + "/cloudmap_lut.png");

    return success;
}
