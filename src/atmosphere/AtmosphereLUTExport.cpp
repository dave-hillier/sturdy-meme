#include "AtmosphereLUTSystem.h"
#include "VmaBuffer.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
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

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(imageSize)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    ManagedBuffer stagingBuffer;
    if (!ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, stagingBuffer)) {
        SDL_Log("Failed to create staging buffer for PNG export");
        return false;
    }

    // Create command buffer for the copy
    vk::Device vkDevice(device);
    auto poolInfo = vk::CommandPoolCreateInfo{}
        .setQueueFamilyIndex(0) // Assuming graphics queue family 0
        .setFlags(vk::CommandPoolCreateFlagBits::eTransient);

    auto poolResult = vkDevice.createCommandPool(poolInfo);
    if (poolResult.result != vk::Result::eSuccess) {
        SDL_Log("Failed to create command pool for PNG export");
        return false;
    }
    vk::CommandPool commandPool = poolResult.value;

    auto allocInfo2 = vk::CommandBufferAllocateInfo{}
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandPool(commandPool)
        .setCommandBufferCount(1);

    auto cmdResult = vkDevice.allocateCommandBuffers(allocInfo2);
    if (cmdResult.result != vk::Result::eSuccess) {
        vkDevice.destroyCommandPool(commandPool);
        SDL_Log("Failed to allocate command buffer for PNG export");
        return false;
    }
    vk::CommandBuffer commandBuffer = cmdResult.value[0];

    auto beginInfo = vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    commandBuffer.begin(beginInfo);

    // Transition image to TRANSFER_SRC_OPTIMAL
    auto toTransferBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
        .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
        .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer,
                          {}, {}, {}, toTransferBarrier);

    // Copy image to buffer
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
        .setImageExtent({width, height, 1});

    commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, stagingBuffer.get(), region);

    // Transition back
    auto toShaderBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, toShaderBarrier);

    commandBuffer.end();

    // Submit and wait
    vk::Queue graphicsQueue = vkDevice.getQueue(0, 0);

    auto submitInfo = vk::SubmitInfo{}
        .setCommandBuffers(commandBuffer);

    graphicsQueue.submit(submitInfo);
    graphicsQueue.waitIdle();

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
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_Log("Failed to map staging buffer for PNG export");
        vkDevice.destroyCommandPool(commandPool);
        return false;
    }

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

    stagingBuffer.unmap();

    // Write PNG
    int result = stbi_write_png(filename.c_str(), width, height, 4, rgba8.data(), width * 4);

    // Cleanup - ManagedBuffer auto-destroys, just need to clean up command pool
    vkDevice.destroyCommandPool(commandPool);

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
