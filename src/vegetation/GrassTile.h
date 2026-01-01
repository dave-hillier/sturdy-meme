#pragma once

#include "GrassConstants.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>
#include <functional>

/**
 * GrassTile - Represents a grass tile in world space
 *
 * Each tile covers a variable size area depending on its LOD level:
 * - LOD 0: 64m x 64m (high detail, near camera)
 * - LOD 1: 128m x 128m (medium detail)
 * - LOD 2: 256m x 256m (low detail, far from camera)
 *
 * Higher LOD tiles have the same grid resolution but cover larger areas,
 * effectively spreading grass blades further apart for lower density.
 *
 * Tiles are streamed around the camera and track their last-used frame
 * for safe GPU resource management with triple buffering.
 *
 * Note: All tiles share a common instance buffer managed by GrassTileManager.
 * This class primarily tracks tile coordinates, LOD level, and usage for streaming.
 */
class GrassTile {
public:
    // Tile coordinate in the world grid (integer tile position)
    // Coordinates are relative to the tile size at each LOD level
    struct TileCoord {
        int x = 0;
        int z = 0;
        uint32_t lod = 0;  // LOD level (0 = high detail, 1 = medium, 2 = low)

        bool operator==(const TileCoord& other) const {
            return x == other.x && z == other.z && lod == other.lod;
        }

        bool operator!=(const TileCoord& other) const {
            return !(*this == other);
        }
    };

    // Tile coordinate hash for use in unordered containers
    struct TileCoordHash {
        std::size_t operator()(const TileCoord& coord) const {
            // Combine x, z, and lod into a single hash
            std::size_t h = std::hash<int>()(coord.x);
            h ^= std::hash<int>()(coord.z) << 16;
            h ^= std::hash<uint32_t>()(coord.lod) << 24;
            return h;
        }
    };

    GrassTile() = default;
    ~GrassTile() = default;

    // Copyable and movable (no GPU resources to manage)
    GrassTile(const GrassTile&) = default;
    GrassTile& operator=(const GrassTile&) = default;
    GrassTile(GrassTile&&) noexcept = default;
    GrassTile& operator=(GrassTile&&) noexcept = default;

    /**
     * Initialize tile with coordinate (includes LOD level)
     */
    void init(TileCoord coord) {
        coord_ = coord;
        lastUsedFrame_ = 0;
    }

    // Accessors
    TileCoord getCoord() const { return coord_; }
    uint32_t getLodLevel() const { return coord_.lod; }

    /**
     * Get the tile size for this tile's LOD level
     */
    float getTileSize() const {
        return GrassConstants::getTileSizeForLod(coord_.lod);
    }

    /**
     * Get the spacing multiplier for this tile's LOD level
     */
    float getSpacingMult() const {
        return GrassConstants::getSpacingMultForLod(coord_.lod);
    }

    /**
     * Get the world-space origin (corner) of this tile
     */
    glm::vec2 getWorldOrigin() const {
        float tileSize = getTileSize();
        return glm::vec2(
            static_cast<float>(coord_.x) * tileSize,
            static_cast<float>(coord_.z) * tileSize
        );
    }

    /**
     * Get the world-space center of this tile
     */
    glm::vec2 getWorldCenter() const {
        return getWorldOrigin() + glm::vec2(getTileSize() * 0.5f);
    }

    /**
     * Check if tile is initialized
     */
    bool isValid() const { return true; }

    /**
     * Calculate squared distance from a world position to tile center
     */
    float distanceSquaredTo(const glm::vec2& worldPos) const {
        glm::vec2 center = getWorldCenter();
        glm::vec2 diff = worldPos - center;
        return glm::dot(diff, diff);
    }

    /**
     * Calculate distance from a world position to tile center
     */
    float distanceTo(const glm::vec2& worldPos) const {
        return std::sqrt(distanceSquaredTo(worldPos));
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
    TileCoord coord_{0, 0, 0};
    uint64_t lastUsedFrame_ = 0;
};
