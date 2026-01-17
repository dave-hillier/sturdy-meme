#pragma once

#include <vulkan/vulkan.hpp>

/**
 * BarrierHelpers - Common pipeline barrier patterns for Vulkan synchronization
 *
 * These helpers reduce boilerplate for frequently-used barrier transitions.
 * They operate on vk::CommandBuffer (vulkan-hpp wrapper).
 *
 * Example usage:
 *   vk::CommandBuffer cmd(rawCmd);
 *   BarrierHelpers::transitionImageLayout(cmd, image,
 *       vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
 *       vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader);
 */
namespace BarrierHelpers {

// ============================================================================
// Image Layout Transitions
// ============================================================================

/**
 * General-purpose image layout transition
 */
inline void transitionImageLayout(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::PipelineStageFlags srcStage,
        vk::PipelineStageFlags dstStage,
        vk::AccessFlags srcAccess = {},
        vk::AccessFlags dstAccess = {},
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = 1,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = 1) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(srcAccess)
        .setDstAccessMask(dstAccess)
        .setOldLayout(oldLayout)
        .setNewLayout(newLayout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(aspectMask)
            .setBaseMipLevel(baseMipLevel)
            .setLevelCount(levelCount)
            .setBaseArrayLayer(baseArrayLayer)
            .setLayerCount(layerCount));

    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

// ============================================================================
// Common Transition Patterns
// ============================================================================

/**
 * Transition image from undefined to general layout (for compute write)
 */
inline void imageToGeneral(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1) {

    transitionImageLayout(cmd, image,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::AccessFlags{},
        vk::AccessFlagBits::eShaderWrite,
        aspectMask, 0, mipLevels);
}

/**
 * Transition image from general to shader read-only (after compute write)
 */
inline void imageToShaderRead(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eFragmentShader,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1) {

    transitionImageLayout(cmd, image,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eComputeShader,
        dstStage,
        vk::AccessFlagBits::eShaderWrite,
        vk::AccessFlagBits::eShaderRead,
        aspectMask, 0, mipLevels);
}

/**
 * Transition image from shader read-only to color attachment (for render pass)
 */
inline void imageToColorAttachment(
        vk::CommandBuffer cmd,
        vk::Image image) {

    transitionImageLayout(cmd, image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::AccessFlagBits::eShaderRead,
        vk::AccessFlagBits::eColorAttachmentWrite);
}

/**
 * Transition image from shader read-only back to general layout (for re-compute)
 */
inline void shaderReadToGeneral(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eFragmentShader,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t mipLevels = 1) {

    transitionImageLayout(cmd, image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eGeneral,
        srcStage,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::AccessFlagBits::eShaderRead,
        vk::AccessFlagBits::eShaderWrite,
        aspectMask, 0, mipLevels);
}

/**
 * Compute shader write barrier (within compute, same image)
 * Use between compute passes that write then read the same image
 */
inline void computeWriteToComputeRead(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::ImageLayout layout = vk::ImageLayout::eGeneral,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = 1) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(layout)
        .setNewLayout(layout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(aspectMask)
            .setBaseMipLevel(baseMipLevel)
            .setLevelCount(levelCount)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {}, barrier);
}

/**
 * Compute write to fragment shader read barrier
 */
inline void computeToFragment(
        vk::CommandBuffer cmd,
        vk::Image image,
        vk::ImageLayout layout = vk::ImageLayout::eGeneral,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(layout)
        .setNewLayout(layout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(aspectMask)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eFragmentShader,
        {}, {}, {}, barrier);
}

// ============================================================================
// Memory Barriers (for buffers)
// ============================================================================

/**
 * Barrier after fillBuffer before compute read/write
 */
inline void fillBufferToCompute(vk::CommandBuffer cmd) {
    auto memBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, memBarrier, {}, {});
}

/**
 * Barrier after compute write before indirect draw
 */
inline void computeToIndirectDraw(vk::CommandBuffer cmd) {
    auto memBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eDrawIndirect,
        {}, memBarrier, {}, {});
}

/**
 * Barrier after compute write before vertex/index read
 */
inline void computeToVertexInput(vk::CommandBuffer cmd) {
    auto memBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eVertexInput,
        {}, memBarrier, {}, {});
}

/**
 * Buffer barrier between compute passes
 */
inline void bufferComputeToCompute(
        vk::CommandBuffer cmd,
        vk::Buffer buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize size = VK_WHOLE_SIZE) {

    auto bufBarrier = vk::BufferMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(buffer)
        .setOffset(offset)
        .setSize(size);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, bufBarrier, {});
}

/**
 * Barrier after compute write before host read
 */
inline void computeToHost(
        vk::CommandBuffer cmd,
        vk::Buffer buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize size = VK_WHOLE_SIZE) {

    auto bufBarrier = vk::BufferMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eHostRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(buffer)
        .setOffset(offset)
        .setSize(size);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eHost,
        {}, {}, bufBarrier, {});
}

// ============================================================================
// Hi-Z / Mip Chain Barriers
// ============================================================================

/**
 * Memory-only barrier between mip level generation passes.
 * Keeps image in General layout - more efficient for iterative mip generation.
 * Use mipChainToShaderRead at the end for final layout transition.
 */
inline void mipMemoryBarrier(
        vk::CommandBuffer cmd,
        vk::Image image,
        uint32_t mipLevel) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eGeneral)  // No layout change
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(mipLevel)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {}, barrier);
}

/**
 * Barrier between mip level generation passes (for Hi-Z or bloom)
 * Transitions a single mip level from write to read
 * @deprecated Use mipMemoryBarrier + mipChainToShaderRead for better performance
 */
inline void mipWriteToRead(
        vk::CommandBuffer cmd,
        vk::Image image,
        uint32_t mipLevel,
        vk::ImageLayout oldLayout = vk::ImageLayout::eGeneral,
        vk::ImageLayout newLayout = vk::ImageLayout::eShaderReadOnlyOptimal) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(oldLayout)
        .setNewLayout(newLayout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(mipLevel)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, {}, {}, barrier);
}

/**
 * Transition entire mip chain to shader read-only
 */
inline void mipChainToShaderRead(
        vk::CommandBuffer cmd,
        vk::Image image,
        uint32_t mipLevelCount,
        vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eComputeShader) {

    auto barrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(mipLevelCount)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        dstStage,
        {}, {}, {}, barrier);
}

} // namespace BarrierHelpers
