#pragma once

#include <vulkan/vulkan.hpp>
#include "QueueSubmitDiagnostics.h"

/**
 * CountingCommandBuffer - A wrapper around vk::CommandBuffer that counts all recorded commands.
 *
 * This wrapper intercepts all Vulkan command recording calls and increments the appropriate
 * counters in QueueSubmitDiagnostics. Use this instead of raw command buffers when you need
 * accurate command statistics.
 *
 * Usage:
 *   CountingCommandBuffer cmd(rawCmd, &diagnostics);
 *   cmd.bindPipeline(...);      // Automatically counted
 *   cmd.draw(...);              // Automatically counted
 *   cmd.dispatch(...);          // Automatically counted
 *
 * The wrapper is lightweight - it just forwards calls to the underlying command buffer
 * while incrementing atomic counters.
 */
class CountingCommandBuffer {
public:
    CountingCommandBuffer(vk::CommandBuffer cmd, QueueSubmitDiagnostics* diag)
        : cmd_(cmd), diag_(diag) {}

    CountingCommandBuffer(VkCommandBuffer cmd, QueueSubmitDiagnostics* diag)
        : cmd_(cmd), diag_(diag) {}

    // Get the underlying command buffer for operations we don't wrap
    vk::CommandBuffer get() const { return cmd_; }
    operator vk::CommandBuffer() const { return cmd_; }
    operator VkCommandBuffer() const { return static_cast<VkCommandBuffer>(cmd_); }

    // === Draw commands ===
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
        cmd_.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed);
    }

    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                     int32_t vertexOffset, uint32_t firstInstance) {
        cmd_.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed);
    }

    void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) {
        cmd_.drawIndirect(buffer, offset, drawCount, stride);
        if (diag_) diag_->drawCallCount.fetch_add(drawCount, std::memory_order_relaxed);
    }

    void drawIndexedIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) {
        cmd_.drawIndexedIndirect(buffer, offset, drawCount, stride);
        if (diag_) diag_->drawCallCount.fetch_add(drawCount, std::memory_order_relaxed);
    }

    void drawIndirectCount(vk::Buffer buffer, vk::DeviceSize offset, vk::Buffer countBuffer,
                           vk::DeviceSize countOffset, uint32_t maxDrawCount, uint32_t stride) {
        cmd_.drawIndirectCount(buffer, offset, countBuffer, countOffset, maxDrawCount, stride);
        // Can't know exact count, estimate with max
        if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed);
    }

    void drawIndexedIndirectCount(vk::Buffer buffer, vk::DeviceSize offset, vk::Buffer countBuffer,
                                  vk::DeviceSize countOffset, uint32_t maxDrawCount, uint32_t stride) {
        cmd_.drawIndexedIndirectCount(buffer, offset, countBuffer, countOffset, maxDrawCount, stride);
        if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed);
    }

    // === Compute commands ===
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        cmd_.dispatch(groupCountX, groupCountY, groupCountZ);
        if (diag_) diag_->dispatchCount.fetch_add(1, std::memory_order_relaxed);
    }

    void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) {
        cmd_.dispatchIndirect(buffer, offset);
        if (diag_) diag_->dispatchCount.fetch_add(1, std::memory_order_relaxed);
    }

    // === Pipeline binding ===
    void bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline) {
        cmd_.bindPipeline(pipelineBindPoint, pipeline);
        if (diag_) diag_->pipelineBindCount.fetch_add(1, std::memory_order_relaxed);
    }

    // === Descriptor set binding ===
    void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout,
                            uint32_t firstSet, uint32_t descriptorSetCount,
                            const vk::DescriptorSet* pDescriptorSets,
                            uint32_t dynamicOffsetCount = 0, const uint32_t* pDynamicOffsets = nullptr) {
        cmd_.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSetCount,
                                pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
        if (diag_) diag_->descriptorSetBindCount.fetch_add(descriptorSetCount, std::memory_order_relaxed);
    }

    void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout,
                            uint32_t firstSet, vk::ArrayProxy<const vk::DescriptorSet> descriptorSets,
                            vk::ArrayProxy<const uint32_t> dynamicOffsets = {}) {
        cmd_.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSets, dynamicOffsets);
        if (diag_) diag_->descriptorSetBindCount.fetch_add(static_cast<uint32_t>(descriptorSets.size()), std::memory_order_relaxed);
    }

    // === Push constants ===
    void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags,
                       uint32_t offset, uint32_t size, const void* pValues) {
        cmd_.pushConstants(layout, stageFlags, offset, size, pValues);
        if (diag_) {
            diag_->pushConstantCount.fetch_add(1, std::memory_order_relaxed);
            diag_->pushConstantBytes.fetch_add(size, std::memory_order_relaxed);
        }
    }

    template<typename T>
    void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags,
                       uint32_t offset, vk::ArrayProxy<const T> values) {
        cmd_.pushConstants(layout, stageFlags, offset, values);
        if (diag_) {
            diag_->pushConstantCount.fetch_add(1, std::memory_order_relaxed);
            diag_->pushConstantBytes.fetch_add(static_cast<uint64_t>(values.size() * sizeof(T)), std::memory_order_relaxed);
        }
    }

    // === Render pass commands ===
    void beginRenderPass(const vk::RenderPassBeginInfo& renderPassBegin, vk::SubpassContents contents) {
        cmd_.beginRenderPass(renderPassBegin, contents);
        if (diag_) diag_->renderPassCount.fetch_add(1, std::memory_order_relaxed);
    }

    void beginRenderPass(const vk::RenderPassBeginInfo* pRenderPassBegin, vk::SubpassContents contents) {
        cmd_.beginRenderPass(pRenderPassBegin, contents);
        if (diag_) diag_->renderPassCount.fetch_add(1, std::memory_order_relaxed);
    }

    void endRenderPass() {
        cmd_.endRenderPass();
    }

    void nextSubpass(vk::SubpassContents contents) {
        cmd_.nextSubpass(contents);
    }

    // === Pipeline barriers ===
    void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask,
                         vk::DependencyFlags dependencyFlags,
                         uint32_t memoryBarrierCount, const vk::MemoryBarrier* pMemoryBarriers,
                         uint32_t bufferMemoryBarrierCount, const vk::BufferMemoryBarrier* pBufferMemoryBarriers,
                         uint32_t imageMemoryBarrierCount, const vk::ImageMemoryBarrier* pImageMemoryBarriers) {
        cmd_.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags,
                             memoryBarrierCount, pMemoryBarriers,
                             bufferMemoryBarrierCount, pBufferMemoryBarriers,
                             imageMemoryBarrierCount, pImageMemoryBarriers);
        if (diag_) {
            diag_->pipelineBarrierCount.fetch_add(1, std::memory_order_relaxed);
            diag_->bufferBarrierCount.fetch_add(bufferMemoryBarrierCount, std::memory_order_relaxed);
            diag_->imageBarrierCount.fetch_add(imageMemoryBarrierCount, std::memory_order_relaxed);
        }
    }

    void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask,
                         vk::DependencyFlags dependencyFlags,
                         vk::ArrayProxy<const vk::MemoryBarrier> memoryBarriers,
                         vk::ArrayProxy<const vk::BufferMemoryBarrier> bufferMemoryBarriers,
                         vk::ArrayProxy<const vk::ImageMemoryBarrier> imageMemoryBarriers) {
        cmd_.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags,
                             memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers);
        if (diag_) {
            diag_->pipelineBarrierCount.fetch_add(1, std::memory_order_relaxed);
            diag_->bufferBarrierCount.fetch_add(static_cast<uint32_t>(bufferMemoryBarriers.size()), std::memory_order_relaxed);
            diag_->imageBarrierCount.fetch_add(static_cast<uint32_t>(imageMemoryBarriers.size()), std::memory_order_relaxed);
        }
    }

    // === Buffer/vertex/index binding (passthrough, not counted as "commands") ===
    void bindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount,
                           const vk::Buffer* pBuffers, const vk::DeviceSize* pOffsets) {
        cmd_.bindVertexBuffers(firstBinding, bindingCount, pBuffers, pOffsets);
    }

    void bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<const vk::Buffer> buffers,
                           vk::ArrayProxy<const vk::DeviceSize> offsets) {
        cmd_.bindVertexBuffers(firstBinding, buffers, offsets);
    }

    void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType) {
        cmd_.bindIndexBuffer(buffer, offset, indexType);
    }

    // === Viewport/scissor (passthrough) ===
    void setViewport(uint32_t firstViewport, uint32_t viewportCount, const vk::Viewport* pViewports) {
        cmd_.setViewport(firstViewport, viewportCount, pViewports);
    }

    void setViewport(uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> viewports) {
        cmd_.setViewport(firstViewport, viewports);
    }

    void setScissor(uint32_t firstScissor, uint32_t scissorCount, const vk::Rect2D* pScissors) {
        cmd_.setScissor(firstScissor, scissorCount, pScissors);
    }

    void setScissor(uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> scissors) {
        cmd_.setScissor(firstScissor, scissors);
    }

    // === Copy commands (passthrough) ===
    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                    uint32_t regionCount, const vk::BufferCopy* pRegions) {
        cmd_.copyBuffer(srcBuffer, dstBuffer, regionCount, pRegions);
    }

    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                    vk::ArrayProxy<const vk::BufferCopy> regions) {
        cmd_.copyBuffer(srcBuffer, dstBuffer, regions);
    }

    void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout,
                           uint32_t regionCount, const vk::BufferImageCopy* pRegions) {
        cmd_.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
    }

    void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout,
                           vk::ArrayProxy<const vk::BufferImageCopy> regions) {
        cmd_.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regions);
    }

    void copyImageToBuffer(vk::Image srcImage, vk::ImageLayout srcImageLayout,
                           vk::Buffer dstBuffer, uint32_t regionCount, const vk::BufferImageCopy* pRegions) {
        cmd_.copyImageToBuffer(srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
    }

    void copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout,
                   vk::Image dstImage, vk::ImageLayout dstImageLayout,
                   uint32_t regionCount, const vk::ImageCopy* pRegions) {
        cmd_.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
    }

    void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout,
                   vk::Image dstImage, vk::ImageLayout dstImageLayout,
                   uint32_t regionCount, const vk::ImageBlit* pRegions, vk::Filter filter) {
        cmd_.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
    }

    void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout,
                   vk::Image dstImage, vk::ImageLayout dstImageLayout,
                   vk::ArrayProxy<const vk::ImageBlit> regions, vk::Filter filter) {
        cmd_.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, filter);
    }

    // === Clear commands (passthrough) ===
    void clearColorImage(vk::Image image, vk::ImageLayout imageLayout,
                         const vk::ClearColorValue& color,
                         uint32_t rangeCount, const vk::ImageSubresourceRange* pRanges) {
        cmd_.clearColorImage(image, imageLayout, color, rangeCount, pRanges);
    }

    void clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout,
                                const vk::ClearDepthStencilValue& depthStencil,
                                uint32_t rangeCount, const vk::ImageSubresourceRange* pRanges) {
        cmd_.clearDepthStencilImage(image, imageLayout, depthStencil, rangeCount, pRanges);
    }

    // === Fill/update buffer (passthrough) ===
    void fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data) {
        cmd_.fillBuffer(dstBuffer, dstOffset, size, data);
    }

    void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData) {
        cmd_.updateBuffer(dstBuffer, dstOffset, dataSize, pData);
    }

    // === Execute secondary command buffers ===
    void executeCommands(uint32_t commandBufferCount, const vk::CommandBuffer* pCommandBuffers) {
        cmd_.executeCommands(commandBufferCount, pCommandBuffers);
    }

    void executeCommands(vk::ArrayProxy<const vk::CommandBuffer> commandBuffers) {
        cmd_.executeCommands(commandBuffers);
    }

    // === Timestamps (passthrough) ===
    void writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query) {
        cmd_.writeTimestamp(pipelineStage, queryPool, query);
    }

    void resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) {
        cmd_.resetQueryPool(queryPool, firstQuery, queryCount);
    }

    // === Debug markers (passthrough) ===
    void beginDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& labelInfo) {
        cmd_.beginDebugUtilsLabelEXT(labelInfo);
    }

    void endDebugUtilsLabelEXT() {
        cmd_.endDebugUtilsLabelEXT();
    }

    void insertDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& labelInfo) {
        cmd_.insertDebugUtilsLabelEXT(labelInfo);
    }

private:
    vk::CommandBuffer cmd_;
    QueueSubmitDiagnostics* diag_;
};
