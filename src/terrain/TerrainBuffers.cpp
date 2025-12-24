#include "TerrainBuffers.h"
#include "UBOs.h"
#include <SDL3/SDL_log.h>
#include <cstring>

bool TerrainBuffers::init(const InitInfo& info) {
    if (!createUniformBuffers(info)) return false;
    if (!createIndirectBuffers(info)) return false;
    return true;
}

void TerrainBuffers::destroy(VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, causticsUniforms);
    BufferUtils::destroyBuffer(allocator, indirectDispatch);
    BufferUtils::destroyBuffer(allocator, indirectDraw);
    BufferUtils::destroyBuffer(allocator, visibleIndices);
    BufferUtils::destroyBuffer(allocator, cullIndirectDispatch);
    BufferUtils::destroyBuffer(allocator, shadowVisible);
    BufferUtils::destroyBuffer(allocator, shadowIndirectDraw);
}

bool TerrainBuffers::createUniformBuffers(const InitInfo& info) {
    // Main terrain uniforms
    if (!BufferUtils::PerFrameBufferBuilder()
        .setAllocator(info.allocator)
        .setFrameCount(info.framesInFlight)
        .setSize(sizeof(TerrainUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain uniform buffers");
        return false;
    }

    // Caustics uniforms (8 floats = 32 bytes, std140 aligned)
    // Matches CausticsUniforms in terrain.frag
    constexpr VkDeviceSize causticsUBOSize = 32;  // 8 * sizeof(float)
    if (!BufferUtils::PerFrameBufferBuilder()
        .setAllocator(info.allocator)
        .setFrameCount(info.framesInFlight)
        .setSize(causticsUBOSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(causticsUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create caustics uniform buffers");
        return false;
    }

    return true;
}

bool TerrainBuffers::createIndirectBuffers(const InitInfo& info) {
    // Indirect dispatch buffer for compute shaders
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(VkDispatchIndirectCommand))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            .setAllocationFlags(0)
            .build(indirectDispatch)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect dispatch buffer");
        return false;
    }

    // Indirect draw buffer for indexed draw commands
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(VkDrawIndexedIndirectCommand))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build(indirectDraw)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect draw buffer");
        return false;
    }

    // Initialize with default values (2 triangles = 6 vertices/indices)
    uint32_t drawArgs[5] = {6, 1, 0, 0, 0};
    memcpy(indirectDraw.mappedPointer, drawArgs, sizeof(drawArgs));

    // Visible indices buffer for stream compaction: [count, index0, index1, ...]
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(uint32_t) * (1 + info.maxVisibleTriangles))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setAllocationFlags(0)
            .build(visibleIndices)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create visible indices buffer");
        return false;
    }

    // Cull indirect dispatch buffer for compute shaders
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(VkDispatchIndirectCommand))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            .setAllocationFlags(0)
            .build(cullIndirectDispatch)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cull indirect dispatch buffer");
        return false;
    }

    // Shadow visible indices buffer
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(uint32_t) * (1 + info.maxVisibleTriangles))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            .setAllocationFlags(0)
            .build(shadowVisible)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow visible indices buffer");
        return false;
    }

    // Shadow indirect draw buffer for indexed draw commands
    if (!BufferUtils::SingleBufferBuilder()
            .setAllocator(info.allocator)
            .setSize(sizeof(VkDrawIndexedIndirectCommand))
            .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            .setAllocationFlags(0)
            .build(shadowIndirectDraw)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow indirect draw buffer");
        return false;
    }

    return true;
}
