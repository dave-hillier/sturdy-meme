#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace VirtualTexture {

// Configuration for the virtual texture system
struct VirtualTextureConfig {
    uint32_t virtualSizePixels = 65536;     // Virtual texture size (64K x 64K)
    uint32_t tileSizePixels = 128;          // Size of each tile (128x128)
    uint32_t cacheSizePixels = 4096;        // Physical cache size (4K x 4K)
    uint32_t borderPixels = 4;              // Tile border for filtering
    uint32_t maxMipLevels = 9;              // log2(512) = 9 mip levels

    // Calculated values
    uint32_t getTilesPerAxis() const {
        return virtualSizePixels / tileSizePixels;
    }

    uint32_t getCacheTilesPerAxis() const {
        return cacheSizePixels / tileSizePixels;
    }

    uint32_t getTotalCacheSlots() const {
        uint32_t perAxis = getCacheTilesPerAxis();
        return perAxis * perAxis;
    }

    uint32_t getTilesAtMip(uint32_t mipLevel) const {
        return getTilesPerAxis() >> mipLevel;
    }

    // Get the virtual UV to world coordinate scale
    float getWorldToVirtualScale(float terrainSize) const {
        return static_cast<float>(virtualSizePixels) / terrainSize;
    }
};

// Unique identifier for a virtual texture tile
struct TileId {
    uint16_t x = 0;         // Virtual tile X coordinate
    uint16_t y = 0;         // Virtual tile Y coordinate
    uint8_t mipLevel = 0;   // Mip level (0 = highest detail)

    TileId() = default;
    TileId(uint16_t x_, uint16_t y_, uint8_t mip) : x(x_), y(y_), mipLevel(mip) {}

    // Pack into 32-bit value for hashing/comparison
    uint32_t pack() const {
        return (static_cast<uint32_t>(mipLevel) << 20) |
               (static_cast<uint32_t>(y) << 10) |
               static_cast<uint32_t>(x);
    }

    // Unpack from 32-bit value
    static TileId unpack(uint32_t packed) {
        TileId id;
        id.x = static_cast<uint16_t>(packed & 0x3FF);
        id.y = static_cast<uint16_t>((packed >> 10) & 0x3FF);
        id.mipLevel = static_cast<uint8_t>((packed >> 20) & 0xFF);
        return id;
    }

    bool operator==(const TileId& other) const {
        return x == other.x && y == other.y && mipLevel == other.mipLevel;
    }

    bool operator!=(const TileId& other) const {
        return !(*this == other);
    }
};

// Page table entry - maps virtual tile to physical cache location
struct PageTableEntry {
    uint16_t cacheX = 0;    // Physical cache X slot
    uint16_t cacheY = 0;    // Physical cache Y slot
    uint8_t valid = 0;      // 0 = not loaded, 1 = loaded

    // Pack for GPU upload (RGBA8)
    uint32_t packRGBA8() const {
        return (static_cast<uint32_t>(valid) << 24) |
               (static_cast<uint32_t>(cacheY) << 8) |
               static_cast<uint32_t>(cacheX);
    }
};

// Cache slot tracking
struct CacheSlot {
    TileId tileId;
    uint32_t lastUsedFrame = 0;
    bool occupied = false;
};

// Feedback entry from GPU - requested tile with priority
struct FeedbackEntry {
    uint32_t tileIdPacked;  // TileId::pack() result
    uint32_t priority;      // Screen-space priority (higher = more important)

    TileId getTileId() const {
        return TileId::unpack(tileIdPacked);
    }
};

// Tile compression format
enum class TileFormat : uint8_t {
    RGBA8 = 0,      // Uncompressed RGBA8
    BC1 = 1,        // BC1/DXT1 compressed (RGB, 4bpp)
    BC1_SRGB = 2,   // BC1 sRGB
    BC4 = 3,        // BC4 compressed (single channel, 4bpp)
    BC5 = 4,        // BC5 compressed (two channels, 8bpp)
    BC7 = 5,        // BC7 compressed (RGBA, 8bpp)
    BC7_SRGB = 6    // BC7 sRGB
};

// Loaded tile data ready for upload
struct LoadedTile {
    TileId id;
    std::vector<uint8_t> pixels;    // RGBA8 or compressed data
    uint32_t width = 0;
    uint32_t height = 0;
    TileFormat format = TileFormat::RGBA8;

    bool isValid() const {
        return !pixels.empty() && width > 0 && height > 0;
    }

    bool isCompressed() const {
        return format != TileFormat::RGBA8;
    }

    // Get bytes per 4x4 block for compressed formats
    uint32_t getBlockSize() const {
        switch (format) {
            case TileFormat::BC1:
            case TileFormat::BC1_SRGB:
            case TileFormat::BC4:
                return 8;
            case TileFormat::BC5:
            case TileFormat::BC7:
            case TileFormat::BC7_SRGB:
                return 16;
            default:
                return 0;
        }
    }
};

// GPU-side parameters for VT sampling (std140 layout)
struct alignas(16) VTParamsUBO {
    glm::vec4 virtualTextureSizeAndInverse;     // xy = size, zw = 1/size
    glm::vec4 physicalCacheSizeAndInverse;      // xy = size, zw = 1/size
    glm::vec4 tileSizeAndBorder;                // x = tile size, y = border, z = tile with border, w = unused
    uint32_t maxMipLevel;
    // Note: Individual uints instead of uint32_t[3] array to match GLSL std140
    // layout (arrays get 16-byte stride per element in std140, scalars don't)
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

} // namespace VirtualTexture

// Hash specialization for TileId
namespace std {
template<>
struct hash<VirtualTexture::TileId> {
    size_t operator()(const VirtualTexture::TileId& id) const {
        return std::hash<uint32_t>{}(id.pack());
    }
};
}
