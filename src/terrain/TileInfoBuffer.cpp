#include "TileInfoBuffer.h"
#include "TerrainTileCache.h"
#include "core/vulkan/VmaBufferFactory.h"
#include <SDL3/SDL.h>

bool TileInfoBuffer::init(const InitInfo& info) {
    maxActiveTiles_ = info.maxActiveTiles;

    // Layout: uint activeTileCount, uint padding[3], TileInfoGPU tiles[maxActiveTiles]
    VkDeviceSize bufferSize = sizeof(uint32_t) * 4 + maxActiveTiles_ * sizeof(TileInfoGPU);
    buffers_.resize(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (!VmaBufferFactory::createStorageBufferHostReadable(info.allocator, bufferSize, buffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TileInfoBuffer: Failed to create tile info buffer %u", i);
            return false;
        }
        mappedPtrs_[i] = buffers_[i].map();
    }

    return true;
}

void TileInfoBuffer::cleanup() {
    buffers_.forEach([](uint32_t, ManagedBuffer& buffer) {
        buffer.reset();
    });
    buffers_.clear();
    mappedPtrs_.fill(nullptr);
}

void TileInfoBuffer::initializeAllFrames() {
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        // Write zero active tile count to each frame buffer
        void* mappedPtr = mappedPtrs_[i];
        if (!mappedPtr) continue;
        uint32_t* countPtr = static_cast<uint32_t*>(mappedPtr);
        countPtr[0] = 0;
        countPtr[1] = 0;
        countPtr[2] = 0;
        countPtr[3] = 0;
    }
}

void TileInfoBuffer::update(uint32_t frameIndex, const std::vector<TerrainTile*>& activeTiles) {
    void* mappedPtr = mappedPtrs_[frameIndex % FRAMES_IN_FLIGHT];
    if (!mappedPtr) return;

    // First 4 uint32s: active tile count + padding
    uint32_t* countPtr = static_cast<uint32_t*>(mappedPtr);
    countPtr[0] = static_cast<uint32_t>(activeTiles.size());
    countPtr[1] = 0;
    countPtr[2] = 0;
    countPtr[3] = 0;

    if (activeTiles.empty()) return;

    // Tile info array follows after the count (offset by 16 bytes for alignment)
    TileInfoGPU* tileInfoArray = reinterpret_cast<TileInfoGPU*>(countPtr + 4);

    for (size_t i = 0; i < activeTiles.size() && i < maxActiveTiles_; i++) {
        TerrainTile* tile = activeTiles[i];

        tileInfoArray[i].worldBounds = glm::vec4(
            tile->worldMinX, tile->worldMinZ,
            tile->worldMaxX, tile->worldMaxZ
        );

        float sizeX = tile->worldMaxX - tile->worldMinX;
        float sizeZ = tile->worldMaxZ - tile->worldMinZ;
        tileInfoArray[i].uvScaleOffset = glm::vec4(
            1.0f / sizeX, 1.0f / sizeZ,
            -tile->worldMinX / sizeX, -tile->worldMinZ / sizeZ
        );

        tileInfoArray[i].layerIndex = glm::ivec4(tile->arrayLayerIndex, 0, 0, 0);
    }
}
