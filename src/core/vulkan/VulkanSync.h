#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

/**
 * Vulkan synchronization utilities using vulkan-hpp.
 *
 * This file provides the core implementations using vulkan-hpp types.
 * For backward compatibility with raw Vulkan C API, use VulkanBarriers.h instead.
 *
 * Key patterns:
 * - Barriers namespace: Standalone barrier functions and image transitions
 * - TrackedImageImpl: Tracks image layout to prevent redundant transitions
 * - BarrierBatchImpl: Batches multiple barriers into a single pipelineBarrier call
 * - ScopedComputeBarrierImpl: RAII guard for compute pass synchronization
 * - ImageBarrierImpl: Fluent builder for image memory barriers
 */

namespace Barriers {

// ============================================================================
// Standalone barrier functions for simple one-off barriers
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
    vk::CommandBuffer cmd,
    vk::Image image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags srcStage,
    vk::PipelineStageFlags dstStage,
    vk::AccessFlags srcAccess,
    vk::AccessFlags dstAccess,
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
    uint32_t baseMip = 0,
    uint32_t mipCount = VK_REMAINING_MIP_LEVELS,
    uint32_t baseLayer = 0,
    uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS)
{
    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(srcAccess)
        .setDstAccessMask(dstAccess)
        .setOldLayout(oldLayout)
        .setNewLayout(newLayout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(aspect)
            .setBaseMipLevel(baseMip)
            .setLevelCount(mipCount)
            .setBaseArrayLayer(baseLayer)
            .setLayerCount(layerCount));

    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

// ============================================================================
// Common image transition patterns
// ============================================================================

inline void prepareImageForCompute(vk::CommandBuffer cmd, vk::Image image,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
        {}, vk::AccessFlagBits::eShaderWrite,
        vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, layerCount);
}

inline void imageComputeToSampling(vk::CommandBuffer cmd, vk::Image image,
                                   vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader,
                                   uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eComputeShader, dstStage,
        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
        vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, layerCount);
}

inline void prepareImageForTransferDst(vk::CommandBuffer cmd, vk::Image image,
                                       uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
        {}, vk::AccessFlagBits::eTransferWrite,
        vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, layerCount);
}

inline void imageTransferToSampling(vk::CommandBuffer cmd, vk::Image image,
                                    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader,
                                    uint32_t mipCount = 1, uint32_t layerCount = 1) {
    transitionImage(cmd, image,
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer, dstStage,
        vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
        vk::ImageAspectFlagBits::eColor, 0, mipCount, 0, layerCount);
}

// ============================================================================
// TrackedImageImpl - RAII image with automatic layout tracking (vulkan-hpp version)
// ============================================================================

class TrackedImageImpl {
public:
    TrackedImageImpl() = default;

    explicit TrackedImageImpl(vk::Image image, vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined,
                              uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                              vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
        : image_(image)
        , currentLayout_(initialLayout)
        , mipLevels_(mipLevels)
        , arrayLayers_(arrayLayers)
        , aspect_(aspect) {}

    vk::Image handle() const { return image_; }
    vk::ImageLayout layout() const { return currentLayout_; }
    uint32_t mipLevels() const { return mipLevels_; }
    uint32_t arrayLayers() const { return arrayLayers_; }

    bool transitionTo(vk::CommandBuffer cmd,
                      vk::ImageLayout newLayout,
                      vk::PipelineStageFlags srcStage,
                      vk::PipelineStageFlags dstStage,
                      vk::AccessFlags srcAccess,
                      vk::AccessFlags dstAccess) {
        if (currentLayout_ == newLayout) {
            return false;
        }

        transitionImage(cmd, image_, currentLayout_, newLayout,
                        srcStage, dstStage, srcAccess, dstAccess,
                        aspect_, 0, mipLevels_, 0, arrayLayers_);

        currentLayout_ = newLayout;
        return true;
    }

    bool prepareForCompute(vk::CommandBuffer cmd) {
        vk::PipelineStageFlags srcStage = (currentLayout_ == vk::ImageLayout::eUndefined)
            ? vk::PipelineStageFlagBits::eTopOfPipe
            : vk::PipelineStageFlagBits::eComputeShader;
        vk::AccessFlags srcAccess = (currentLayout_ == vk::ImageLayout::eUndefined)
            ? vk::AccessFlags{}
            : vk::AccessFlagBits::eShaderRead;

        return transitionTo(cmd, vk::ImageLayout::eGeneral,
                            srcStage, vk::PipelineStageFlagBits::eComputeShader,
                            srcAccess, vk::AccessFlagBits::eShaderWrite);
    }

    bool prepareForSampling(vk::CommandBuffer cmd,
                            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader) {
        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eComputeShader;
        vk::AccessFlags srcAccess = vk::AccessFlagBits::eShaderWrite;

        if (currentLayout_ == vk::ImageLayout::eTransferDstOptimal) {
            srcStage = vk::PipelineStageFlagBits::eTransfer;
            srcAccess = vk::AccessFlagBits::eTransferWrite;
        }

        return transitionTo(cmd, vk::ImageLayout::eShaderReadOnlyOptimal,
                            srcStage, dstStage,
                            srcAccess, vk::AccessFlagBits::eShaderRead);
    }

    bool prepareForTransferDst(vk::CommandBuffer cmd) {
        vk::PipelineStageFlags srcStage = (currentLayout_ == vk::ImageLayout::eUndefined)
            ? vk::PipelineStageFlagBits::eTopOfPipe
            : vk::PipelineStageFlagBits::eAllCommands;
        vk::AccessFlags srcAccess = (currentLayout_ == vk::ImageLayout::eUndefined)
            ? vk::AccessFlags{}
            : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;

        return transitionTo(cmd, vk::ImageLayout::eTransferDstOptimal,
                            srcStage, vk::PipelineStageFlagBits::eTransfer,
                            srcAccess, vk::AccessFlagBits::eTransferWrite);
    }

    void setLayoutWithoutBarrier(vk::ImageLayout layout) {
        currentLayout_ = layout;
    }

private:
    vk::Image image_;
    vk::ImageLayout currentLayout_ = vk::ImageLayout::eUndefined;
    uint32_t mipLevels_ = 1;
    uint32_t arrayLayers_ = 1;
    vk::ImageAspectFlags aspect_ = vk::ImageAspectFlagBits::eColor;
};

// ============================================================================
// BarrierBatchImpl - Batch multiple barriers into a single call (vulkan-hpp version)
// ============================================================================

class BarrierBatchImpl {
public:
    explicit BarrierBatchImpl(vk::CommandBuffer cmd,
                              vk::PipelineStageFlags srcStages = {},
                              vk::PipelineStageFlags dstStages = {})
        : cmd_(cmd)
        , srcStages_(srcStages)
        , dstStages_(dstStages) {}

    ~BarrierBatchImpl() {
        submit();
    }

    BarrierBatchImpl(const BarrierBatchImpl&) = delete;
    BarrierBatchImpl& operator=(const BarrierBatchImpl&) = delete;

    BarrierBatchImpl& imageTransition(
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
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(srcAccess)
            .setDstAccessMask(dstAccess)
            .setOldLayout(oldLayout)
            .setNewLayout(newLayout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(aspect)
                .setBaseMipLevel(baseMip)
                .setLevelCount(mipCount)
                .setBaseArrayLayer(baseLayer)
                .setLayerCount(layerCount));

        imageBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatchImpl& bufferBarrier(
        vk::Buffer buffer,
        vk::AccessFlags srcAccess,
        vk::AccessFlags dstAccess,
        vk::DeviceSize offset = 0,
        vk::DeviceSize size = VK_WHOLE_SIZE)
    {
        auto barrier = vk::BufferMemoryBarrier{}
            .setSrcAccessMask(srcAccess)
            .setDstAccessMask(dstAccess)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(buffer)
            .setOffset(offset)
            .setSize(size);

        bufferBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatchImpl& memoryBarrier(vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) {
        auto barrier = vk::MemoryBarrier{}
            .setSrcAccessMask(srcAccess)
            .setDstAccessMask(dstAccess);

        memoryBarriers_.push_back(barrier);
        srcStages_ |= accessToSrcStage(srcAccess);
        dstStages_ |= accessToDstStage(dstAccess);
        return *this;
    }

    BarrierBatchImpl& setStages(vk::PipelineStageFlags src, vk::PipelineStageFlags dst) {
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

        if (!srcStages_) srcStages_ = vk::PipelineStageFlagBits::eAllCommands;
        if (!dstStages_) dstStages_ = vk::PipelineStageFlagBits::eAllCommands;

        cmd_.pipelineBarrier(srcStages_, dstStages_, {},
            memoryBarriers_, bufferBarriers_, imageBarriers_);
    }

private:
    static vk::PipelineStageFlags accessToSrcStage(vk::AccessFlags access) {
        if (!access) return vk::PipelineStageFlagBits::eTopOfPipe;
        if (access & vk::AccessFlagBits::eTransferWrite) return vk::PipelineStageFlagBits::eTransfer;
        if (access & vk::AccessFlagBits::eTransferRead) return vk::PipelineStageFlagBits::eTransfer;
        if (access & (vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite))
            return vk::PipelineStageFlagBits::eComputeShader;
        if (access & vk::AccessFlagBits::eColorAttachmentWrite)
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        return vk::PipelineStageFlagBits::eAllCommands;
    }

    static vk::PipelineStageFlags accessToDstStage(vk::AccessFlags access) {
        if (!access) return vk::PipelineStageFlagBits::eBottomOfPipe;
        if (access & vk::AccessFlagBits::eTransferWrite) return vk::PipelineStageFlagBits::eTransfer;
        if (access & vk::AccessFlagBits::eTransferRead) return vk::PipelineStageFlagBits::eTransfer;
        if (access & vk::AccessFlagBits::eIndirectCommandRead)
            return vk::PipelineStageFlagBits::eDrawIndirect;
        if (access & vk::AccessFlagBits::eVertexAttributeRead)
            return vk::PipelineStageFlagBits::eVertexInput;
        if (access & (vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite))
            return vk::PipelineStageFlagBits::eComputeShader;
        return vk::PipelineStageFlagBits::eAllCommands;
    }

    vk::CommandBuffer cmd_;
    vk::PipelineStageFlags srcStages_;
    vk::PipelineStageFlags dstStages_;
    std::vector<vk::MemoryBarrier> memoryBarriers_;
    std::vector<vk::BufferMemoryBarrier> bufferBarriers_;
    std::vector<vk::ImageMemoryBarrier> imageBarriers_;
    bool submitted_ = false;
};

// ============================================================================
// ScopedComputeBarrierImpl - RAII guard for compute pass synchronization (vulkan-hpp version)
// ============================================================================

class ScopedComputeBarrierImpl {
public:
    explicit ScopedComputeBarrierImpl(vk::CommandBuffer cmd,
                                      vk::AccessFlags dstAccess = vk::AccessFlagBits::eShaderRead)
        : cmd_(cmd)
        , dstAccess_(dstAccess) {}

    ~ScopedComputeBarrierImpl() {
        if (!skipped_) {
            auto barrier = vk::MemoryBarrier{}
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(dstAccess_);
            cmd_.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, barrier, {}, {});
        }
    }

    ScopedComputeBarrierImpl(const ScopedComputeBarrierImpl&) = delete;
    ScopedComputeBarrierImpl& operator=(const ScopedComputeBarrierImpl&) = delete;

    void skip() { skipped_ = true; }

private:
    vk::CommandBuffer cmd_;
    vk::AccessFlags dstAccess_;
    bool skipped_ = false;
};

// ============================================================================
// ImageBarrierImpl - Fluent builder for single image barriers (vulkan-hpp version)
// ============================================================================

class ImageBarrierImpl {
public:
    ImageBarrierImpl(vk::CommandBuffer cmd, vk::Image image)
        : cmd_(cmd) {
        barrier_.setImage(image)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
    }

    ImageBarrierImpl& from(vk::ImageLayout layout) {
        barrier_.setOldLayout(layout);
        return *this;
    }

    ImageBarrierImpl& to(vk::ImageLayout layout) {
        barrier_.setNewLayout(layout);
        return *this;
    }

    ImageBarrierImpl& srcAccess(vk::AccessFlags access) {
        barrier_.setSrcAccessMask(access);
        return *this;
    }

    ImageBarrierImpl& dstAccess(vk::AccessFlags access) {
        barrier_.setDstAccessMask(access);
        return *this;
    }

    ImageBarrierImpl& mipLevels(uint32_t base, uint32_t count) {
        auto range = barrier_.subresourceRange;
        range.setBaseMipLevel(base).setLevelCount(count);
        barrier_.setSubresourceRange(range);
        return *this;
    }

    ImageBarrierImpl& arrayLayers(uint32_t base, uint32_t count) {
        auto range = barrier_.subresourceRange;
        range.setBaseArrayLayer(base).setLayerCount(count);
        barrier_.setSubresourceRange(range);
        return *this;
    }

    ImageBarrierImpl& aspect(vk::ImageAspectFlags flags) {
        auto range = barrier_.subresourceRange;
        range.setAspectMask(flags);
        barrier_.setSubresourceRange(range);
        return *this;
    }

    ImageBarrierImpl& stages(vk::PipelineStageFlags src, vk::PipelineStageFlags dst) {
        srcStage_ = src;
        dstStage_ = dst;
        return *this;
    }

    ImageBarrierImpl& forCompute() {
        srcStage_ = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage_ = vk::PipelineStageFlagBits::eComputeShader;
        return *this;
    }

    ImageBarrierImpl& computeToCompute() {
        srcStage_ = vk::PipelineStageFlagBits::eComputeShader;
        dstStage_ = vk::PipelineStageFlagBits::eComputeShader;
        return *this;
    }

    ImageBarrierImpl& computeToFragment() {
        srcStage_ = vk::PipelineStageFlagBits::eComputeShader;
        dstStage_ = vk::PipelineStageFlagBits::eFragmentShader;
        return *this;
    }

    void submit() {
        cmd_.pipelineBarrier(srcStage_, dstStage_, {}, {}, {}, barrier_);
    }

private:
    vk::CommandBuffer cmd_;
    vk::ImageMemoryBarrier barrier_;
    vk::PipelineStageFlags srcStage_ = vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage_ = vk::PipelineStageFlagBits::eAllCommands;
};

// ============================================================================
// High-level operations combining barriers with commands
// ============================================================================

inline void copyBufferToImage(
    vk::CommandBuffer cmd,
    vk::Buffer stagingBuffer,
    vk::Image image,
    uint32_t width,
    uint32_t height,
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader)
{
    prepareImageForTransferDst(cmd, image);

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

    cmd.copyBufferToImage(stagingBuffer, image,
        vk::ImageLayout::eTransferDstOptimal, region);

    imageTransferToSampling(cmd, image, dstStage);
}

inline void copyBufferToImageRegion(
    vk::CommandBuffer cmd,
    vk::Buffer stagingBuffer,
    vk::Image image,
    int32_t offsetX,
    int32_t offsetY,
    uint32_t width,
    uint32_t height)
{
    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
        .setImageOffset({offsetX, offsetY, 0})
        .setImageExtent({width, height, 1});

    cmd.copyBufferToImage(stagingBuffer, image,
        vk::ImageLayout::eTransferDstOptimal, region);
}

inline void copyBufferToImageLayer(
    vk::CommandBuffer cmd,
    vk::Buffer stagingBuffer,
    vk::Image image,
    uint32_t width,
    uint32_t height,
    uint32_t arrayLayer)
{
    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(arrayLayer)
            .setLayerCount(1))
        .setImageOffset({0, 0, 0})
        .setImageExtent({width, height, 1});

    cmd.copyBufferToImage(stagingBuffer, image,
        vk::ImageLayout::eTransferDstOptimal, region);
}

inline void clearBufferForCompute(
    vk::CommandBuffer cmd,
    vk::Buffer buffer,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = sizeof(uint32_t))
{
    cmd.fillBuffer(buffer, offset, size, 0);
    transferToCompute(cmd);
}

inline void clearBufferForComputeReadWrite(
    vk::CommandBuffer cmd,
    vk::Buffer buffer,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = sizeof(uint32_t))
{
    cmd.fillBuffer(buffer, offset, size, 0);
    auto barrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {});
}

inline void clearBufferForFragment(
    vk::CommandBuffer cmd,
    vk::Buffer buffer,
    vk::DeviceSize offset = 0,
    vk::DeviceSize size = sizeof(uint32_t))
{
    cmd.fillBuffer(buffer, offset, size, 0);
    transferToFragmentRead(cmd);
}

} // namespace Barriers
