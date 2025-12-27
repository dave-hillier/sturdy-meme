#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <span>

/**
 * RAII-based Vulkan barrier utilities to prevent common synchronization bugs.
 *
 * Key patterns:
 * - TrackedImage: Tracks image layout to prevent redundant transitions
 * - BarrierBatch: Batches multiple barriers into a single vkCmdPipelineBarrier call
 * - ScopedComputeBarrier: RAII guard for compute pass synchronization
 * - ImageBarrier: Fluent builder for image memory barriers
 *
 * Usage examples:
 *
 *   // TrackedImage - automatic layout tracking
 *   TrackedImage lut(image, VK_IMAGE_LAYOUT_UNDEFINED);
 *   lut.prepareForCompute(cmd);  // Only transitions if needed
 *   vkCmdDispatch(cmd, ...);
 *   lut.prepareForSampling(cmd);
 *
 *   // BarrierBatch - batch multiple barriers
 *   {
 *       BarrierBatch batch(cmd);
 *       batch.imageTransition(img1, oldLayout, newLayout, srcAccess, dstAccess);
 *       batch.imageTransition(img2, oldLayout, newLayout, srcAccess, dstAccess);
 *   }  // Single vkCmdPipelineBarrier call here
 *
 *   // ScopedComputeBarrier - ensures exit barrier
 *   {
 *       ScopedComputeBarrier guard(cmd);
 *       vkCmdDispatch(cmd, ...);
 *   }  // Compute-to-compute barrier automatically inserted
 */

namespace Barriers {

// ============================================================================
// Standalone barrier functions for simple one-off barriers
// ============================================================================

/**
 * Global memory barrier between compute shader stages.
 * Use when you need to synchronize shader writes before reads in subsequent dispatches.
 */
inline void computeToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Global memory barrier from compute writes to both reads and writes.
 * Use when subsequent compute passes may both read and write.
 */
inline void computeToComputeReadWrite(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize compute shader output for indirect draw consumption.
 * Use before vkCmdDrawIndirect/vkCmdDrawIndexedIndirect when buffers are
 * written by compute shaders.
 */
inline void computeToIndirectDraw(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize compute shader output for vertex shader storage buffer reads and indirect draw.
 * Use for particle systems where compute shaders write instance data that vertex shaders
 * read as storage buffers, combined with indirect draw commands.
 *
 * This differs from computeToIndirectDraw by targeting the vertex shader stage for
 * storage buffer reads rather than vertex input stage for vertex attributes.
 */
inline void computeToVertexAndIndirectDraw(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize compute shader output for fragment shader sampling.
 * Use when transitioning from compute writes to texture sampling in fragment shaders.
 */
inline void computeToFragmentRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize transfer operations before compute shader access.
 * Use after buffer/image copies when compute shaders will read the data.
 */
inline void transferToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize transfer operations before fragment shader access.
 * Use after texture uploads when textures will be sampled in fragment shaders.
 */
inline void transferToFragmentRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize transfer operations before CPU host access.
 * Use after vkCmdCopyBuffer to a host-visible readback buffer
 * when the CPU needs to read the results.
 */
inline void transferToHostRead(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize CPU host writes before compute shader access.
 * Use when CPU writes to persistently mapped buffers that compute shaders will read.
 * This ensures host writes are visible to the GPU before the compute dispatch.
 */
inline void hostToCompute(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Synchronize transfer operations before vertex input stage.
 * Use after vkCmdCopyBuffer to vertex/index buffers when they
 * will be bound for drawing.
 */
inline void transferToVertexInput(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
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

/**
 * Transition a single image between layouts.
 *
 * @param cmd Command buffer to record the barrier
 * @param image The image to transition
 * @param oldLayout Current layout (use VK_IMAGE_LAYOUT_UNDEFINED if unknown)
 * @param newLayout Target layout
 * @param srcStage Pipeline stage that produces the data
 * @param dstStage Pipeline stage that consumes the data
 * @param srcAccess Access type of the source operations
 * @param dstAccess Access type of the destination operations
 * @param aspect Image aspect (default: COLOR_BIT)
 * @param baseMip Base mip level (default: 0)
 * @param mipCount Number of mip levels (default: VK_REMAINING_MIP_LEVELS)
 * @param baseLayer Base array layer (default: 0)
 * @param layerCount Number of array layers (default: VK_REMAINING_ARRAY_LAYERS)
 */
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
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ============================================================================
// Common image transition patterns
// ============================================================================

/**
 * Prepare an image for compute shader writes.
 * Transitions from UNDEFINED to GENERAL layout.
 */
inline void prepareImageForCompute(VkCommandBuffer cmd, VkImage image,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

/**
 * Transition image from compute write to shader read.
 * Transitions from GENERAL to SHADER_READ_ONLY_OPTIMAL.
 */
inline void imageComputeToSampling(VkCommandBuffer cmd, VkImage image,
                                   VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStage,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

/**
 * Prepare image for transfer destination (e.g., before vkCmdCopyBufferToImage).
 */
inline void prepareImageForTransferDst(VkCommandBuffer cmd, VkImage image,
                                       uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, layerCount);
}

/**
 * Transition image from transfer destination to shader sampling.
 */
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

/**
 * RAII wrapper that tracks image layout and prevents redundant transitions.
 *
 * Benefits:
 * - Automatically skips transitions when already in correct layout
 * - Provides semantic methods for common operations
 * - Makes current layout always queryable
 *
 * Note: This is a non-owning wrapper - does not destroy the VkImage.
 */
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

    /**
     * Transition to a new layout, only if not already in that layout.
     * Returns true if a transition was performed.
     */
    bool transitionTo(VkCommandBuffer cmd,
                      VkImageLayout newLayout,
                      VkPipelineStageFlags srcStage,
                      VkPipelineStageFlags dstStage,
                      VkAccessFlags srcAccess,
                      VkAccessFlags dstAccess) {
        if (currentLayout_ == newLayout) {
            return false;  // Already in correct layout
        }

        transitionImage(cmd, image_, currentLayout_, newLayout,
                        srcStage, dstStage, srcAccess, dstAccess,
                        aspect_, 0, mipLevels_, 0, arrayLayers_);

        currentLayout_ = newLayout;
        return true;
    }

    /**
     * Prepare for compute shader writes (transition to GENERAL).
     */
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

    /**
     * Prepare for sampling in fragment shaders (transition to SHADER_READ_ONLY_OPTIMAL).
     */
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

    /**
     * Prepare for transfer destination operations.
     */
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

    /**
     * Force set the layout without inserting a barrier.
     * Use only when you know the layout has changed externally.
     */
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

/**
 * RAII batch builder for combining multiple barriers into a single vkCmdPipelineBarrier call.
 *
 * Barriers are accumulated and submitted when the batch is destroyed.
 * This is more efficient than multiple individual barrier calls.
 */
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

    // Non-copyable, non-movable
    BarrierBatch(const BarrierBatch&) = delete;
    BarrierBatch& operator=(const BarrierBatch&) = delete;

    /**
     * Add an image layout transition to the batch.
     */
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
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;

        imageBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    /**
     * Add a buffer memory barrier to the batch.
     */
    BarrierBatch& bufferBarrier(
        VkBuffer buffer,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkDeviceSize offset = 0,
        VkDeviceSize size = VK_WHOLE_SIZE)
    {
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
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

    /**
     * Add a global memory barrier to the batch.
     */
    BarrierBatch& memoryBarrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;

        memoryBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    /**
     * Explicitly set pipeline stages (overrides auto-detection).
     */
    BarrierBatch& setStages(VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        srcStages_ = src;
        dstStages_ = dst;
        return *this;
    }

    /**
     * Submit the batched barriers immediately (also called automatically in destructor).
     */
    void submit() {
        if (submitted_) return;
        submitted_ = true;

        if (memoryBarriers_.empty() && bufferBarriers_.empty() && imageBarriers_.empty()) {
            return;
        }

        // Ensure we have valid stages
        if (srcStages_ == 0) srcStages_ = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (dstStages_ == 0) dstStages_ = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        vkCmdPipelineBarrier(cmd_,
            srcStages_, dstStages_, 0,
            static_cast<uint32_t>(memoryBarriers_.size()), memoryBarriers_.data(),
            static_cast<uint32_t>(bufferBarriers_.size()), bufferBarriers_.data(),
            static_cast<uint32_t>(imageBarriers_.size()), imageBarriers_.data());
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
    VkPipelineStageFlags srcStages_ = 0;
    VkPipelineStageFlags dstStages_ = 0;
    std::vector<VkMemoryBarrier> memoryBarriers_;
    std::vector<VkBufferMemoryBarrier> bufferBarriers_;
    std::vector<VkImageMemoryBarrier> imageBarriers_;
    bool submitted_ = false;
};

// ============================================================================
// ScopedComputeBarrier - RAII guard for compute pass synchronization
// ============================================================================

/**
 * RAII guard that inserts a compute-to-compute barrier on destruction.
 *
 * Use this to ensure compute passes are properly synchronized without
 * manually remembering to insert exit barriers.
 */
class ScopedComputeBarrier {
public:
    explicit ScopedComputeBarrier(VkCommandBuffer cmd,
                                  VkAccessFlags dstAccess = VK_ACCESS_SHADER_READ_BIT)
        : cmd_(cmd)
        , dstAccess_(dstAccess) {}

    ~ScopedComputeBarrier() {
        if (!skipped_) {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = dstAccess_;
            vkCmdPipelineBarrier(cmd_,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }

    // Non-copyable, non-movable
    ScopedComputeBarrier(const ScopedComputeBarrier&) = delete;
    ScopedComputeBarrier& operator=(const ScopedComputeBarrier&) = delete;

    /**
     * Skip the exit barrier (e.g., if the pass was not actually executed).
     */
    void skip() { skipped_ = true; }

private:
    VkCommandBuffer cmd_;
    VkAccessFlags dstAccess_;
    bool skipped_ = false;
};

// ============================================================================
// ImageBarrier - Fluent builder for single image barriers
// ============================================================================

/**
 * Fluent builder for constructing and submitting a single image memory barrier.
 *
 * Example:
 *   ImageBarrier(cmd, image)
 *       .from(VK_IMAGE_LAYOUT_UNDEFINED)
 *       .to(VK_IMAGE_LAYOUT_GENERAL)
 *       .accessNone()
 *       .accessWrite()
 *       .forCompute()
 *       .submit();
 */
class ImageBarrier {
public:
    ImageBarrier(VkCommandBuffer cmd, VkImage image)
        : cmd_(cmd) {
        barrier_.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_.pNext = nullptr;
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

    // Convenience: set up for compute shader access
    ImageBarrier& forCompute() {
        srcStage_ = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        return *this;
    }

    // Convenience: set up for compute-to-compute transition
    ImageBarrier& computeToCompute() {
        srcStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        return *this;
    }

    // Convenience: set up for compute-to-fragment transition
    ImageBarrier& computeToFragment() {
        srcStage_ = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage_ = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        return *this;
    }

    void submit() {
        vkCmdPipelineBarrier(cmd_, srcStage_, dstStage_,
                             0, 0, nullptr, 0, nullptr, 1, &barrier_);
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

/**
 * Copy staging buffer to image with automatic barrier transitions.
 * Handles: UNDEFINED -> TRANSFER_DST -> copy -> SHADER_READ_ONLY
 *
 * @param cmd Command buffer to record commands
 * @param stagingBuffer Source buffer with image data
 * @param image Destination image
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param dstStage Pipeline stage that will read the image (default: fragment shader)
 */
inline void copyBufferToImage(
    VkCommandBuffer cmd,
    VkBuffer stagingBuffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
{
    // Transition to transfer destination
    prepareImageForTransferDst(cmd, image);

    // Set up copy region for standard 2D image
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

    // Transition to shader read
    imageTransferToSampling(cmd, image, dstStage);
}

/**
 * Copy staging buffer to a specific region of an image.
 * Use when updating a sub-region (e.g., virtual texture tiles).
 * Caller is responsible for layout transitions.
 *
 * @param cmd Command buffer to record commands
 * @param stagingBuffer Source buffer with image data
 * @param image Destination image (must be in TRANSFER_DST_OPTIMAL)
 * @param offsetX X offset in destination image
 * @param offsetY Y offset in destination image
 * @param width Region width in pixels
 * @param height Region height in pixels
 */
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

/**
 * Copy staging buffer to an array layer of an image.
 * Use for texture arrays where each layer is uploaded separately.
 *
 * @param cmd Command buffer to record commands
 * @param stagingBuffer Source buffer with image data
 * @param image Destination image array (must be in TRANSFER_DST_OPTIMAL)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param arrayLayer Target array layer index
 */
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

/**
 * Clear a buffer to zero and insert barrier for compute shader access.
 * Common pattern for resetting counters before compute dispatches.
 *
 * @param cmd Command buffer to record commands
 * @param buffer Buffer to clear
 * @param offset Offset in buffer (default: 0)
 * @param size Size to clear (default: sizeof(uint32_t) for counters)
 */
inline void clearBufferForCompute(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    transferToCompute(cmd);
}

/**
 * Clear a buffer to zero and insert barrier for compute shader read/write access.
 * Use when compute shaders will both read and write to the cleared buffer.
 *
 * @param cmd Command buffer to record commands
 * @param buffer Buffer to clear
 * @param offset Offset in buffer (default: 0)
 * @param size Size to clear (default: sizeof(uint32_t) for counters)
 */
inline void clearBufferForComputeReadWrite(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

/**
 * Clear a buffer to zero and insert barrier for fragment shader access.
 * Use when fragment shaders will read/write the cleared buffer.
 *
 * @param cmd Command buffer to record commands
 * @param buffer Buffer to clear
 * @param offset Offset in buffer (default: 0)
 * @param size Size to clear (default: sizeof(uint32_t) for counters)
 */
inline void clearBufferForFragment(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset = 0,
    VkDeviceSize size = sizeof(uint32_t))
{
    vkCmdFillBuffer(cmd, buffer, offset, size, 0);
    transferToFragmentRead(cmd);
}

}  // namespace Barriers
