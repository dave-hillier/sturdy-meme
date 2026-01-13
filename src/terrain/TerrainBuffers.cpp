#include "TerrainBuffers.h"
#include "UBOs.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <utility>

std::unique_ptr<TerrainBuffers> TerrainBuffers::create(const InitInfo& info) {
    std::unique_ptr<TerrainBuffers> buffers(new TerrainBuffers());
    if (!buffers->initInternal(info)) {
        return nullptr;
    }
    return buffers;
}

TerrainBuffers::~TerrainBuffers() {
    if (allocator_) {
        BufferUtils::destroyBuffers(allocator_, uniformBuffers);
        BufferUtils::destroyBuffers(allocator_, causticsUniforms);
        BufferUtils::destroyBuffers(allocator_, liquidUniforms);
        BufferUtils::destroyBuffers(allocator_, materialLayerUniforms);
        BufferUtils::destroyBuffer(allocator_, indirectDispatch);
        BufferUtils::destroyBuffer(allocator_, indirectDraw);
        BufferUtils::destroyBuffer(allocator_, visibleIndices);
        BufferUtils::destroyBuffer(allocator_, cullIndirectDispatch);
        BufferUtils::destroyBuffer(allocator_, shadowVisible);
        BufferUtils::destroyBuffer(allocator_, shadowIndirectDraw);
    }
}

TerrainBuffers::TerrainBuffers(TerrainBuffers&& other) noexcept
    : allocator_(other.allocator_)
    , uniformBuffers(std::move(other.uniformBuffers))
    , indirectDispatch(std::move(other.indirectDispatch))
    , indirectDraw(std::move(other.indirectDraw))
    , visibleIndices(std::move(other.visibleIndices))
    , cullIndirectDispatch(std::move(other.cullIndirectDispatch))
    , shadowVisible(std::move(other.shadowVisible))
    , shadowIndirectDraw(std::move(other.shadowIndirectDraw))
    , causticsUniforms(std::move(other.causticsUniforms))
    , liquidUniforms(std::move(other.liquidUniforms))
    , materialLayerUniforms(std::move(other.materialLayerUniforms))
{
    other.allocator_ = VK_NULL_HANDLE;
}

TerrainBuffers& TerrainBuffers::operator=(TerrainBuffers&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (allocator_) {
            BufferUtils::destroyBuffers(allocator_, uniformBuffers);
            BufferUtils::destroyBuffers(allocator_, causticsUniforms);
            BufferUtils::destroyBuffers(allocator_, liquidUniforms);
            BufferUtils::destroyBuffers(allocator_, materialLayerUniforms);
            BufferUtils::destroyBuffer(allocator_, indirectDispatch);
            BufferUtils::destroyBuffer(allocator_, indirectDraw);
            BufferUtils::destroyBuffer(allocator_, visibleIndices);
            BufferUtils::destroyBuffer(allocator_, cullIndirectDispatch);
            BufferUtils::destroyBuffer(allocator_, shadowVisible);
            BufferUtils::destroyBuffer(allocator_, shadowIndirectDraw);
        }

        // Move from other
        allocator_ = other.allocator_;
        uniformBuffers = std::move(other.uniformBuffers);
        indirectDispatch = std::move(other.indirectDispatch);
        indirectDraw = std::move(other.indirectDraw);
        visibleIndices = std::move(other.visibleIndices);
        cullIndirectDispatch = std::move(other.cullIndirectDispatch);
        shadowVisible = std::move(other.shadowVisible);
        shadowIndirectDraw = std::move(other.shadowIndirectDraw);
        causticsUniforms = std::move(other.causticsUniforms);
        liquidUniforms = std::move(other.liquidUniforms);
        materialLayerUniforms = std::move(other.materialLayerUniforms);

        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool TerrainBuffers::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    if (!createUniformBuffers(info)) return false;
    if (!createIndirectBuffers(info)) return false;
    return true;
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

    // Liquid uniforms (composable material system - puddles, wetness)
    // Matches TerrainLiquidUniforms in terrain.frag and TerrainLiquidUBO in C++
    constexpr VkDeviceSize liquidUBOSize = 128;  // Aligned size of TerrainLiquidUBO
    if (!BufferUtils::PerFrameBufferBuilder()
        .setAllocator(info.allocator)
        .setFrameCount(info.framesInFlight)
        .setSize(liquidUBOSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(liquidUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create liquid uniform buffers");
        return false;
    }

    // Material layer uniforms (composable material system - layer blending)
    // Matches MaterialLayerUBO in MaterialLayer.h: 4 layers * 5 vec4s + int + padding
    // LayerData = 5 * vec4 = 80 bytes, 4 layers = 320 bytes + 16 bytes header = 336 bytes
    constexpr VkDeviceSize materialLayerUBOSize = 336;
    if (!BufferUtils::PerFrameBufferBuilder()
        .setAllocator(info.allocator)
        .setFrameCount(info.framesInFlight)
        .setSize(materialLayerUBOSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(materialLayerUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create material layer uniform buffers");
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
