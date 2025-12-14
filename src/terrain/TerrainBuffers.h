#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "BufferUtils.h"
#include <cstdint>

class TerrainBuffers {
public:
    struct InitInfo {
        VmaAllocator allocator;
        uint32_t framesInFlight;
        uint32_t maxVisibleTriangles;
    };

    TerrainBuffers() = default;
    ~TerrainBuffers() = default;

    bool init(const InitInfo& info);
    void destroy(VmaAllocator allocator);

    // Uniform buffer accessors
    VkBuffer getUniformBuffer(uint32_t frameIndex) const { return uniformBuffers.buffers[frameIndex]; }
    void* getUniformMappedPtr(uint32_t frameIndex) const { return uniformBuffers.mappedPointers[frameIndex]; }

    // Indirect buffer accessors
    VkBuffer getIndirectDispatchBuffer() const { return indirectDispatch.buffer; }
    VkBuffer getIndirectDrawBuffer() const { return indirectDraw.buffer; }
    void* getIndirectDrawMappedPtr() const { return indirectDraw.mappedPointer; }

    // Visibility buffer accessors (stream compaction)
    VkBuffer getVisibleIndicesBuffer() const { return visibleIndices.buffer; }
    VkBuffer getCullIndirectDispatchBuffer() const { return cullIndirectDispatch.buffer; }

    // Shadow buffer accessors
    VkBuffer getShadowVisibleBuffer() const { return shadowVisible.buffer; }
    VkBuffer getShadowIndirectDrawBuffer() const { return shadowIndirectDraw.buffer; }

private:
    bool createUniformBuffers(const InitInfo& info);
    bool createIndirectBuffers(const InitInfo& info);

    // Per-frame uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Indirect dispatch/draw buffers
    BufferUtils::SingleBuffer indirectDispatch;
    BufferUtils::SingleBuffer indirectDraw;

    // Stream compaction buffers
    BufferUtils::SingleBuffer visibleIndices;
    BufferUtils::SingleBuffer cullIndirectDispatch;

    // Shadow culling buffers
    BufferUtils::SingleBuffer shadowVisible;
    BufferUtils::SingleBuffer shadowIndirectDraw;
};
