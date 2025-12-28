#pragma once

// ============================================================================
// VulkanBarriers.h - Pure C API Barrier Utilities
// ============================================================================
// This file provides barrier utilities using the raw Vulkan C API.
// For vulkan-hpp types (vk::CommandBuffer, vk::Image, etc.), use VulkanSync.h instead.

#include <vulkan/vulkan.h>
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

    VkImage handle() const { return image_; }
    VkImageLayout layout() const { return currentLayout_; }
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
