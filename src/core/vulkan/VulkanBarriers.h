#pragma once

// ============================================================================
// VulkanBarriers.h - Vulkan Barrier Utilities
// ============================================================================
// This file provides barrier utilities with both raw Vulkan C API and vulkan-hpp
// overloads. All functions accept either VkCommandBuffer or vk::CommandBuffer.

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vector>

/**
 * RAII-based Vulkan barrier utilities to prevent common synchronization bugs.
 *
 * Key patterns:
 * - TrackedImage: Tracks image layout to prevent redundant transitions
 * - BarrierBatch: Batches multiple barriers into a single vkCmdPipelineBarrier call
 * - ScopedComputeBarrier: RAII guard for compute pass synchronization
 * - ImageBarrier: Fluent builder for image memory barriers
 */

namespace Barriers {

// ============================================================================
// Standalone barrier functions for simple one-off barriers
// ============================================================================

inline void computeToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void computeToComputeReadWrite(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void computeToIndirectDraw(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void computeToVertexAndIndirectDraw(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void computeToFragmentRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void transferToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void transferToFragmentRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void transferToHostRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void hostToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void transferToVertexInput(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// ============================================================================
// Standalone barrier functions - vk::CommandBuffer overloads
// ============================================================================

inline void computeToCompute(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {});
}

inline void computeToComputeReadWrite(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {});
}

inline void computeToIndirectDraw(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eVertexAttributeRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexInput,
        {}, barrier, {}, {});
}

inline void computeToVertexAndIndirectDraw(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
        {}, barrier, {}, {});
}

inline void computeToFragmentRead(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eFragmentShader,
        {}, barrier, {}, {});
}

inline void transferToCompute(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {});
}

inline void transferToFragmentRead(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader,
        {}, barrier, {}, {});
}

inline void transferToHostRead(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eHostRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eHost,
        {}, barrier, {}, {});
}

inline void hostToCompute(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {});
}

inline void transferToVertexInput(vk::CommandBuffer cmd) {
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eVertexInput,
        {}, barrier, {}, {});
}

// ============================================================================
// Image layout transition helper
// ============================================================================

inline void transitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t baseMip = 0,
    uint32_t mipCount = VK_REMAINING_MIP_LEVELS,
    uint32_t baseLayer = 0,
    uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = baseLayer;
    barrier.subresourceRange.layerCount = layerCount;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ============================================================================
// Common image transition patterns
// ============================================================================

inline void prepareImageForCompute(VkCommandBuffer cmd, VkImage image,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

inline void imageComputeToSampling(VkCommandBuffer cmd, VkImage image,
                                   VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStage,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

inline void prepareImageForTransferDst(VkCommandBuffer cmd, VkImage image,
                                       uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

inline void imageTransferToSampling(VkCommandBuffer cmd, VkImage image,
                                    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                    uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

// ============================================================================
// Image layout transition helper - vk::CommandBuffer overload
// ============================================================================
// Note: Functions that take multiple Vulkan handle types (image, buffer) use
// only the C API overloads to avoid ambiguity. The vk::CommandBuffer can be
// implicitly converted to VkCommandBuffer, so callers using vk:: types should
// pass handles as VkImage/VkBuffer directly or use static_cast on the command buffer.

// ============================================================================
// TrackedImage - RAII image with automatic layout tracking
// ============================================================================

class TrackedImage {
public:
    TrackedImage() = default;

    explicit TrackedImage(VkImage image, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
        : image_(image)
        , currentLayout_(initialLayout)
        , mipLevels_(mipLevels)
        , arrayLayers_(arrayLayers)
        , aspect_(aspect) {}

    // vk:: constructor overload
    explicit TrackedImage(vk::Image image, vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined,
                          uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                          vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
        : image_(static_cast<VkImage>(image))
        , currentLayout_(static_cast<VkImageLayout>(initialLayout))
        , mipLevels_(mipLevels)
        , arrayLayers_(arrayLayers)
        , aspect_(static_cast<VkImageAspectFlags>(aspect)) {}

    VkImage handle() const { return image_; }
    vk::Image vkHandle() const { return vk::Image(image_); }
    VkImageLayout layout() const { return currentLayout_; }
    vk::ImageLayout vkLayout() const { return static_cast<vk::ImageLayout>(currentLayout_); }
    uint32_t mipLevels() const { return mipLevels_; }
    uint32_t arrayLayers() const { return arrayLayers_; }

    bool transitionTo(VkCommandBuffer cmd,
                      VkImageLayout newLayout,
                      VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage,
                      VkAccessFlags srcAccess,
                      VkAccessFlags dstAccess) {
        if (currentLayout_ == newLayout) {
            return false;
        }

        transitionImage(cmd, image_, currentLayout_, newLayout,
                        srcStage, dstStage, srcAccess, dstAccess,
                        aspect_, 0, mipLevels_, 0, arrayLayers_);

        currentLayout_ = newLayout;
        return true;
    }

    bool prepareForCompute(VkCommandBuffer cmd) {
        VkPipelineStageFlags srcStage = (currentLayout_ == VK_IMAGE_LAYOUT_UNDEFINED)
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkAccessFlags srcAccess = (currentLayout_ == VK_IMAGE_LAYOUT_UNDEFINED)
            ? 0
            : VK_ACCESS_SHADER_READ_BIT;

        return transitionTo(cmd, VK_IMAGE_LAYOUT_GENERAL,
                            srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            srcAccess, VK_ACCESS_SHADER_WRITE_BIT);
    }

    bool prepareForSampling(VkCommandBuffer cmd,
                            VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) {
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkAccessFlags srcAccess = VK_ACCESS_SHADER_WRITE_BIT;

        if (currentLayout_ == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        }

        return transitionTo(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            srcStage, dstStage,
                            srcAccess, VK_ACCESS_SHADER_READ_BIT);
    }

    bool prepareForTransferDst(VkCommandBuffer cmd) {
        VkPipelineStageFlags srcStage = (currentLayout_ == VK_IMAGE_LAYOUT_UNDEFINED)
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkAccessFlags srcAccess = (currentLayout_ == VK_IMAGE_LAYOUT_UNDEFINED)
            ? 0
            : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        return transitionTo(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            srcAccess, VK_ACCESS_TRANSFER_WRITE_BIT);
    }

    void setLayoutWithoutBarrier(VkImageLayout layout) {
        currentLayout_ = layout;
    }

    // vk::CommandBuffer overloads
    bool transitionTo(vk::CommandBuffer cmd,
                      vk::ImageLayout newLayout,
                      vk::PipelineStageFlags srcStage,
                      vk::PipelineStageFlags dstStage,
                      vk::AccessFlags srcAccess,
                      vk::AccessFlags dstAccess) {
        return transitionTo(static_cast<VkCommandBuffer>(cmd),
                            static_cast<VkImageLayout>(newLayout),
                            static_cast<VkPipelineStageFlags>(srcStage),
                            static_cast<VkPipelineStageFlags>(dstStage),
                            static_cast<VkAccessFlags>(srcAccess),
                            static_cast<VkAccessFlags>(dstAccess));
    }

    bool prepareForCompute(vk::CommandBuffer cmd) {
        return prepareForCompute(static_cast<VkCommandBuffer>(cmd));
    }

    bool prepareForSampling(vk::CommandBuffer cmd,
                            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader) {
        return prepareForSampling(static_cast<VkCommandBuffer>(cmd),
                                  static_cast<VkPipelineStageFlags>(dstStage));
    }

    bool prepareForTransferDst(vk::CommandBuffer cmd) {
        return prepareForTransferDst(static_cast<VkCommandBuffer>(cmd));
    }

    void setLayoutWithoutBarrier(vk::ImageLayout layout) {
        currentLayout_ = static_cast<VkImageLayout>(layout);
    }

private:
    VkImage image_ = VK_NULL_HANDLE;
    VkImageLayout currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t mipLevels_ = 1;
    uint32_t arrayLayers_ = 1;
    VkImageAspectFlags aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
};

// ============================================================================
// BarrierBatch - Batch multiple barriers into a single call
// ============================================================================

class BarrierBatch {
public:
    explicit BarrierBatch(VkCommandBuffer cmd,
                          VkPipelineStageFlags srcStages = 0,
                          VkPipelineStageFlags dstStages = 0)
        : cmd_(cmd)
        , srcStages_(srcStages)
        , dstStages_(dstStages) {}

    // vk::CommandBuffer constructor overload
    explicit BarrierBatch(vk::CommandBuffer cmd,
                          vk::PipelineStageFlags srcStages = {},
                          vk::PipelineStageFlags dstStages = {})
        : cmd_(static_cast<VkCommandBuffer>(cmd))
        , srcStages_(static_cast<VkPipelineStageFlags>(srcStages))
        , dstStages_(static_cast<VkPipelineStageFlags>(dstStages)) {}

    ~BarrierBatch() {
        submit();
    }

    BarrierBatch(const BarrierBatch&) = delete;
    BarrierBatch& operator=(const BarrierBatch&) = delete;

    BarrierBatch& imageTransition(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        uint32_t baseMip = 0,
        uint32_t mipCount = 1,
        uint32_t baseLayer = 0,
        uint32_t layerCount = 1)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = baseMip;
        barrier.subresourceRange.levelCount = mipCount;
        barrier.subresourceRange.baseArrayLayer = baseLayer;
        barrier.subresourceRange.layerCount = layerCount;

        imageBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatch& bufferBarrier(
        VkBuffer buffer,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkDeviceSize offset = 0,
        VkDeviceSize size = VK_WHOLE_SIZE)
    {
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.offset = offset;
        barrier.size = size;

        bufferBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatch& memoryBarrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;

        memoryBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatch& setStages(VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        srcStages_ = src;
        dstStages_ = dst;
        return *this;
    }

    // vk:: overloads
    BarrierBatch& imageTransition(
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags srcAccess,
        vk::AccessFlags dstAccess,
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
        uint32_t baseMip = 0,
        uint32_t mipCount = 1,
        uint32_t baseLayer = 0,
        uint32_t layerCount = 1)
    {
        return imageTransition(
            static_cast<VkImage>(image),
            static_cast<VkImageLayout>(oldLayout),
            static_cast<VkImageLayout>(newLayout),
            static_cast<VkAccessFlags>(srcAccess),
            static_cast<VkAccessFlags>(dstAccess),
            static_cast<VkImageAspectFlags>(aspect),
            baseMip, mipCount, baseLayer, layerCount);
    }

    BarrierBatch& bufferBarrier(
        vk::Buffer buffer,
        vk::AccessFlags srcAccess,
        vk::AccessFlags dstAccess,
        vk::DeviceSize offset = 0,
        vk::DeviceSize size = VK_WHOLE_SIZE)
    {
        return bufferBarrier(
            static_cast<VkBuffer>(buffer),
            static_cast<VkAccessFlags>(srcAccess),
            static_cast<VkAccessFlags>(dstAccess),
            static_cast<VkDeviceSize>(offset),
            static_cast<VkDeviceSize>(size));
    }

    BarrierBatch& memoryBarrier(vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) {
        return memoryBarrier(
            static_cast<VkAccessFlags>(srcAccess),
            static_cast<VkAccessFlags>(dstAccess));
    }

    BarrierBatch& setStages(vk::PipelineStageFlags src, vk::PipelineStageFlags dst) {
        srcStages_ = static_cast<VkPipelineStageFlags>(src);
        dstStages_ = static_cast<VkPipelineStageFlags>(dst);
        return *this;
    }

    void submit() {
        if (submitted_) return;
        submitted_ = true;

        if (memoryBarriers_.empty() && bufferBarriers_.empty() && imageBarriers_.empty()) {
            return;
        }

        if (srcStages_ == 0) srcStages_ = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (dstStages_ == 0) dstStages_ = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        vkCmdPipelineBarrier(cmd_, srcStages_, dstStages_, 0,
            static_cast<uint32_t>(memoryBarriers_.size()),
            memoryBarriers_.empty() ? nullptr : memoryBarriers_.data(),
            static_cast<uint32_t>(bufferBarriers_.size()),
            bufferBarriers_.empty() ? nullptr : bufferBarriers_.data(),
            static_cast<uint32_t>(imageBarriers_.size()),
            imageBarriers_.empty() ? nullptr : imageBarriers_.data());
    }

private:
    static VkPipelineStageFlags accessToSrcStage(VkAccessFlags access) {
        if (access == 0) return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (access & VK_ACCESS_TRANSFER_WRITE_BIT) return VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (access & VK_ACCESS_TRANSFER_READ_BIT) return VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT))
            return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (access & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    static VkPipelineStageFlags accessToDstStage(VkAccessFlags access) {
        if (access == 0) return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        if (access & VK_ACCESS_TRANSFER_WRITE_BIT) return VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (access & VK_ACCESS_TRANSFER_READ_BIT) return VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (access & VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
            return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        if (access & VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)
            return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        if (access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT))
            return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    VkCommandBuffer cmd_;
    VkPipelineStageFlags srcStages_;
    VkPipelineStageFlags dstStages_;
    std::vector<VkMemoryBarrier> memoryBarriers_;
    std::vector<VkBufferMemoryBarrier> bufferBarriers_;
    std::vector<VkImageMemoryBarrier> imageBarriers_;
    bool submitted_ = false;
};

// ============================================================================
// ScopedComputeBarrier - RAII guard for compute pass synchronization
// ============================================================================

class ScopedComputeBarrier {
public:
    explicit ScopedComputeBarrier(VkCommandBuffer cmd,
                                  VkAccessFlags dstAccess = VK_ACCESS_SHADER_READ_BIT)
        : cmd_(cmd)
        , dstAccess_(dstAccess) {}

    // vk::CommandBuffer constructor overload
    explicit ScopedComputeBarrier(vk::CommandBuffer cmd,
                                  vk::AccessFlags dstAccess = vk::AccessFlagBits::eShaderRead)
        : cmd_(static_cast<VkCommandBuffer>(cmd))
        , dstAccess_(static_cast<VkAccessFlags>(dstAccess)) {}

    ~ScopedComputeBarrier() {
        if (!skipped_) {
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = dstAccess_;
            vkCmdPipelineBarrier(cmd_,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }

    ScopedComputeBarrier(const ScopedComputeBarrier&) = delete;
    ScopedComputeBarrier& operator=(const ScopedComputeBarrier&) = delete;

    void skip() { skipped_ = true; }

private:
    VkCommandBuffer cmd_;
    VkAccessFlags dstAccess_;
    bool skipped_ = false;
};

// ============================================================================
// ImageBarrier - Fluent builder for single image barriers
// ============================================================================

class ImageBarrier {
public:
    ImageBarrier(VkCommandBuffer cmd, VkImage image)
        : cmd_(cmd) {
        barrier_.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_.image = image;
        barrier_.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_.subresourceRange.baseMipLevel = 0;
        barrier_.subresourceRange.levelCount = 1;
        barrier_.subresourceRange.baseArrayLayer = 0;
        barrier_.subresourceRange.layerCount = 1;
    }

    // vk:: constructor overload
    ImageBarrier(vk::CommandBuffer cmd, vk::Image image)
        : cmd_(static_cast<VkCommandBuffer>(cmd)) {
        barrier_.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier_.image = static_cast<VkImage>(image);
        barrier_.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier_.subresourceRange.baseMipLevel = 0;
        barrier_.subresourceRange.levelCount = 1;
        barrier_.subresourceRange.baseArrayLayer = 0;
        barrier_.subresourceRange.layerCount = 1;
    }

    ImageBarrier& from(VkImageLayout layout) {
        barrier_.oldLayout = layout;
        return *this;
    }

    ImageBarrier& to(VkImageLayout layout) {
        barrier_.newLayout = layout;
        return *this;
    }

    ImageBarrier& srcAccess(VkAccessFlags access) {
        barrier_.srcAccessMask = access;
        return *this;
    }

    ImageBarrier& dstAccess(VkAccessFlags access) {
        barrier_.dstAccessMask = access;
        return *this;
    }

    ImageBarrier& mipLevels(uint32_t base, uint32_t count) {
        barrier_.subresourceRange.baseMipLevel = base;
        barrier_.subresourceRange.levelCount = count;
        return *this;
    }

    ImageBarrier& arrayLayers(uint32_t base, uint32_t count) {
        barrier_.subresourceRange.baseArrayLayer = base;
        barrier_.subresourceRange.layerCount = count;
        return *this;
    }

    ImageBarrier& aspect(VkImageAspectFlags flags) {
        barrier_.subresourceRange.aspectMask = flags;
        return *this;
    }

    ImageBarrier& stages(VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        srcStage_ = src;
        dstStage_ = dst;
        return *this;
    }

    ImageBarrier& forCompute() {
        srcStage_ = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        return *this;
    }

    ImageBarrier& computeToCompute() {
        srcStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        return *this;
    }

    ImageBarrier& computeToFragment() {
        srcStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage_ = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        return *this;
    }

    // vk:: overloads
    ImageBarrier& from(vk::ImageLayout layout) {
        barrier_.oldLayout = static_cast<VkImageLayout>(layout);
        return *this;
    }

    ImageBarrier& to(vk::ImageLayout layout) {
        barrier_.newLayout = static_cast<VkImageLayout>(layout);
        return *this;
    }

    ImageBarrier& srcAccess(vk::AccessFlags access) {
        barrier_.srcAccessMask = static_cast<VkAccessFlags>(access);
        return *this;
    }

    ImageBarrier& dstAccess(vk::AccessFlags access) {
        barrier_.dstAccessMask = static_cast<VkAccessFlags>(access);
        return *this;
    }

    ImageBarrier& aspect(vk::ImageAspectFlags flags) {
        barrier_.subresourceRange.aspectMask = static_cast<VkImageAspectFlags>(flags);
        return *this;
    }

    ImageBarrier& stages(vk::PipelineStageFlags src, vk::PipelineStageFlags dst) {
        srcStage_ = static_cast<VkPipelineStageFlags>(src);
        dstStage_ = static_cast<VkPipelineStageFlags>(dst);
        return *this;
    }

    void submit() {
        vkCmdPipelineBarrier(cmd_, srcStage_, dstStage_, 0, 0, nullptr, 0, nullptr, 1, &barrier_);
    }

private:
    VkCommandBuffer cmd_;
    VkImageMemoryBarrier barrier_{};
    VkPipelineStageFlags srcStage_ = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage_ = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
};

// ============================================================================
// High-level operations combining barriers with commands
// ============================================================================

inline void copyBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer stagingBuffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
{
    prepareImageForTransferDst(cmd, image);

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

    vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    imageTransferToSampling(cmd, image, dstStage);
}

inline void copyBufferToImageRegion(
    VkCommandBuffer cmd,
    VkBuffer stagingBuffer,
    VkImage image,
    int32_t offsetX,
    int32_t offsetY,
    uint32_t width,
    uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {offsetX, offsetY, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

inline void copyBufferToImageLayer(
    VkCommandBuffer cmd,
    VkBuffer stagingBuffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    uint32_t arrayLayer)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

inline void clearBufferForCompute(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    transferToCompute(cmd);
}

inline void clearBufferForComputeReadWrite(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void clearBufferForFragment(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    transferToFragmentRead(cmd);
}

} // namespace Barriers
