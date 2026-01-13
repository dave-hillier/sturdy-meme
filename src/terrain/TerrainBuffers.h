#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "BufferUtils.h"
#include <cstdint>
#include <memory>

class TerrainBuffers {
public:
    struct InitInfo {
        VmaAllocator allocator;
        uint32_t framesInFlight;
        uint32_t maxVisibleTriangles;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<TerrainBuffers> create(const InitInfo& info);

    ~TerrainBuffers();

    // Move-only
    TerrainBuffers(TerrainBuffers&& other) noexcept;
    TerrainBuffers& operator=(TerrainBuffers&& other) noexcept;
    TerrainBuffers(const TerrainBuffers&) = delete;
    TerrainBuffers& operator=(const TerrainBuffers&) = delete;

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

    // Caustics UBO accessors
    VkBuffer getCausticsUniformBuffer(uint32_t frameIndex) const { return causticsUniforms.buffers[frameIndex]; }
    void* getCausticsMappedPtr(uint32_t frameIndex) const { return causticsUniforms.mappedPointers[frameIndex]; }

    // Liquid UBO accessors (composable material system - puddles, wet surfaces)
    VkBuffer getLiquidUniformBuffer(uint32_t frameIndex) const { return liquidUniforms.buffers[frameIndex]; }
    void* getLiquidMappedPtr(uint32_t frameIndex) const { return liquidUniforms.mappedPointers[frameIndex]; }

private:
    TerrainBuffers() = default;
    bool initInternal(const InitInfo& info);
    bool createUniformBuffers(const InitInfo& info);
    bool createIndirectBuffers(const InitInfo& info);

    // Stored for cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;

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

    // Caustics uniform buffers (per-frame for underwater caustics)
    BufferUtils::PerFrameBufferSet causticsUniforms;

    // Liquid uniform buffers (composable material system - puddles, wetness)
    BufferUtils::PerFrameBufferSet liquidUniforms;
};
