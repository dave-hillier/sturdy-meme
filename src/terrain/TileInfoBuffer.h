#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include "core/vulkan/VmaBuffer.h"
#include "core/FrameBuffered.h"

struct TerrainTile;

// Tile info for GPU (matches shader buffer layout)
struct TileInfoGPU {
    glm::vec4 worldBounds;  // xy = min corner, zw = max corner
    glm::vec4 uvScaleOffset; // xy = scale, zw = offset (for UV calculation)
    glm::ivec4 layerIndex;  // x = layer index in tile array, yzw = padding (std140 alignment)
};

// Manages the triple-buffered tile info storage buffer used by shaders
// to look up which array layer corresponds to which world region.
class TileInfoBuffer {
public:
    static constexpr uint32_t FRAMES_IN_FLIGHT = TripleBuffered<int>::DEFAULT_FRAME_COUNT;

    struct InitInfo {
        VmaAllocator allocator = VK_NULL_HANDLE;
        uint32_t maxActiveTiles = 64;
    };

    TileInfoBuffer() = default;
    ~TileInfoBuffer() = default;

    TileInfoBuffer(const TileInfoBuffer&) = delete;
    TileInfoBuffer& operator=(const TileInfoBuffer&) = delete;
    TileInfoBuffer(TileInfoBuffer&&) = delete;
    TileInfoBuffer& operator=(TileInfoBuffer&&) = delete;

    bool init(const InitInfo& info);
    void cleanup();

    // Update the buffer for the current frame with the given active tiles
    void update(uint32_t frameIndex, const std::vector<TerrainTile*>& activeTiles);

    // Initialize all frame buffers to zero active tiles
    void initializeAllFrames();

    VkBuffer getBuffer(uint32_t frameIndex) const {
        return buffers_.at(frameIndex).get();
    }

private:
    uint32_t maxActiveTiles_ = 64;

    TripleBuffered<ManagedBuffer> buffers_;
    std::array<void*, FRAMES_IN_FLIGHT> mappedPtrs_ = {};
};
