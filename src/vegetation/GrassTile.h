#pragma once

#include "GrassConstants.h"
#include "BufferUtils.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

/**
 * GrassTile - Manages a single grass tile's GPU resources
 *
 * Each tile covers a TILE_SIZE x TILE_SIZE area of the world and contains
 * its own instance buffer and indirect draw buffer for grass blades.
 *
 * The tile system follows Ghost of Tsushima's approach:
 * - Tiles are streamed around the camera
 * - Each tile has its own compute dispatch
 * - Double-buffering: compute to one set while rendering from another
 */
class GrassTile {
public:
    // Tile coordinate in the world grid (integer tile position)
    struct TileCoord {
        int x = 0;
        int z = 0;

        bool operator==(const TileCoord& other) const {
            return x == other.x && z == other.z;
        }

        bool operator!=(const TileCoord& other) const {
            return !(*this == other);
        }
    };

    // Tile coordinate hash for use in unordered containers
    struct TileCoordHash {
        std::size_t operator()(const TileCoord& coord) const {
            // Combine x and z into a single hash
            return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.z) << 16);
        }
    };

    GrassTile() = default;
    ~GrassTile();

    // Non-copyable
    GrassTile(const GrassTile&) = delete;
    GrassTile& operator=(const GrassTile&) = delete;

    // Movable
    GrassTile(GrassTile&& other) noexcept;
    GrassTile& operator=(GrassTile&& other) noexcept;

    /**
     * Initialize tile resources
     * @param allocator VMA allocator for buffer creation
     * @param coord Tile coordinate in world grid
     * @param bufferSetCount Number of buffer sets (for double-buffering)
     * @return true on success
     */
    bool init(VmaAllocator allocator, TileCoord coord, uint32_t bufferSetCount);

    /**
     * Destroy tile resources
     */
    void destroy();

    // Accessors
    TileCoord getCoord() const { return coord_; }

    /**
     * Get the world-space origin (corner) of this tile
     */
    glm::vec2 getWorldOrigin() const {
        return glm::vec2(
            static_cast<float>(coord_.x) * GrassConstants::TILE_SIZE,
            static_cast<float>(coord_.z) * GrassConstants::TILE_SIZE
        );
    }

    /**
     * Get the world-space center of this tile
     */
    glm::vec2 getWorldCenter() const {
        return getWorldOrigin() + glm::vec2(GrassConstants::TILE_SIZE * 0.5f);
    }

    /**
     * Get instance buffer for a buffer set
     */
    vk::Buffer getInstanceBuffer(uint32_t setIndex) const {
        return instanceBuffers_.buffers[setIndex];
    }

    /**
     * Get indirect buffer for a buffer set
     */
    vk::Buffer getIndirectBuffer(uint32_t setIndex) const {
        return indirectBuffers_.buffers[setIndex];
    }

    /**
     * Check if tile is initialized
     */
    bool isValid() const { return allocator_ != VK_NULL_HANDLE; }

    /**
     * Calculate squared distance from a world position to tile center
     */
    float distanceSquaredTo(const glm::vec2& worldPos) const {
        glm::vec2 center = getWorldCenter();
        glm::vec2 diff = worldPos - center;
        return glm::dot(diff, diff);
    }

    /**
     * Mark the tile as used this frame (for unload tracking)
     */
    void markUsed(uint64_t frameNumber) { lastUsedFrame_ = frameNumber; }

    /**
     * Get the last frame this tile was used
     */
    uint64_t getLastUsedFrame() const { return lastUsedFrame_; }

    /**
     * Check if tile is safe to unload (hasn't been used for N frames)
     * Uses triple buffering - wait at least 3 frames to ensure GPU isn't using it
     */
    bool canUnload(uint64_t currentFrame, uint32_t framesInFlight) const {
        return (currentFrame - lastUsedFrame_) > framesInFlight;
    }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    TileCoord coord_{0, 0};
    uint64_t lastUsedFrame_ = 0;

    // Per-tile buffers (triple-buffered to match frames in flight)
    BufferUtils::DoubleBufferedBufferSet instanceBuffers_;
    BufferUtils::DoubleBufferedBufferSet indirectBuffers_;
};
