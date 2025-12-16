#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include "VulkanRAII.h"

// Tile coordinate in the grid
struct TileCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const TileCoord& other) const {
        return x == other.x && z == other.z;
    }
};

// Hash function for TileCoord to use in unordered_map
struct TileCoordHash {
    size_t operator()(const TileCoord& coord) const {
        return std::hash<int64_t>()(static_cast<int64_t>(coord.x) << 32 | static_cast<uint32_t>(coord.z));
    }
};

// A single terrain tile with CPU and GPU data
struct TerrainTile {
    TileCoord coord;
    uint32_t lod = 0;

    // CPU data for collision queries (512x512 normalized heights)
    std::vector<float> cpuData;

    // GPU resources
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;

    // World bounds (for shader lookup)
    float worldMinX = 0.0f;
    float worldMinZ = 0.0f;
    float worldMaxX = 0.0f;
    float worldMaxZ = 0.0f;

    bool loaded = false;
};

// Tile info for GPU (matches shader buffer layout)
struct TileInfoGPU {
    glm::vec4 worldBounds;  // xy = min corner, zw = max corner
    glm::vec4 uvScaleOffset; // xy = scale, zw = offset (for UV calculation)
};

// Terrain tile cache - manages LOD-based tile streaming
class TerrainTileCache {
public:
    struct InitInfo {
        std::string cacheDirectory;
        VkDevice device;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        float terrainSize;      // Total terrain size in world units
        float heightScale;      // Height scale for altitude conversion
        float minAltitude;      // Minimum altitude (for height value 0)
        float maxAltitude;      // Maximum altitude (for height value 65535)
    };

    TerrainTileCache() = default;
    ~TerrainTileCache() = default;

    // Initialize - loads metadata from terrain_tiles.meta
    bool init(const InitInfo& info);

    // Cleanup all GPU resources
    void destroy();

    // Update active tiles based on camera position
    // Loads tiles within loadRadius, unloads tiles beyond unloadRadius
    void updateActiveTiles(const glm::vec3& cameraPos, float loadRadius, float unloadRadius);

    // Get height at world position from loaded tiles
    // Returns true if a tile covers this position and sets outHeight
    // Returns false if no tile covers this position (caller should use global fallback)
    bool getHeightAt(float worldX, float worldZ, float& outHeight) const;

    // Get the tile coordinate for a world position at a given LOD
    TileCoord worldToTileCoord(float worldX, float worldZ, uint32_t lod) const;

    // Check if a tile is currently loaded
    bool isTileLoaded(TileCoord coord, uint32_t lod) const;

    // Get sampler for tile textures
    VkSampler getSampler() const { return sampler.get(); }

    // Get tile array image view (sampler2DArray)
    VkImageView getTileArrayView() const { return tileArrayView; }

    // Get active tile count and descriptors for shader binding
    uint32_t getActiveTileCount() const { return static_cast<uint32_t>(activeTiles.size()); }
    const std::vector<TerrainTile*>& getActiveTiles() const { return activeTiles; }

    // Get tile info buffer for shader
    VkBuffer getTileInfoBuffer() const { return tileInfoBuffer_.get(); }

    // Accessors
    uint32_t getNumLODLevels() const { return numLODLevels; }
    uint32_t getTileResolution() const { return tileResolution; }
    uint32_t getTilesX() const { return tilesX; }
    uint32_t getTilesZ() const { return tilesZ; }
    float getTerrainSize() const { return terrainSize; }
    float getHeightScale() const { return heightScale; }
    float getMinAltitude() const { return minAltitude; }

    // Get a loaded tile by coordinate and LOD (returns nullptr if not loaded)
    const TerrainTile* getLoadedTile(TileCoord coord, uint32_t lod) const;

    // Request a tile to be loaded (for physics pre-loading)
    // Returns true if tile is now loaded (or was already loaded)
    bool requestTileLoad(TileCoord coord, uint32_t lod);

    // Load only CPU data for a tile (no GPU resources) - for physics during early init
    // Returns true if cpuData is available after this call
    bool loadTileCPUOnly(TileCoord coord, uint32_t lod);

    // LOD distance thresholds (can be configured)
    static constexpr float LOD0_MAX_DISTANCE = 1000.0f;  // < 1km: LOD0
    static constexpr float LOD1_MAX_DISTANCE = 2000.0f;  // 1-2km: LOD1
    static constexpr float LOD2_MAX_DISTANCE = 4000.0f;  // 2-4km: LOD2
    static constexpr float LOD3_MAX_DISTANCE = 8000.0f;  // 4-8km: LOD3

private:
    // Load a single tile from disk at native resolution (NO downsampling)
    bool loadTile(TileCoord coord, uint32_t lod);

    // Unload a tile and free GPU resources
    void unloadTile(TileCoord coord, uint32_t lod);

    // Create GPU texture for a tile
    bool createTileGPUResources(TerrainTile& tile);

    // Upload tile data to GPU
    bool uploadTileToGPU(TerrainTile& tile);

    // Update tile info buffer for shaders
    void updateTileInfoBuffer();

    // Copy tile data to a specific layer of the tile array texture
    void copyTileToArrayLayer(TerrainTile* tile, uint32_t layerIndex);

    // Get appropriate LOD level for distance
    uint32_t getLODForDistance(float distance) const;

    // Get tile path in cache
    std::string getTilePath(TileCoord coord, uint32_t lod) const;

    // Parse metadata file
    bool loadMetadata();

    // Make a unique key for tile lookup
    uint64_t makeTileKey(TileCoord coord, uint32_t lod) const;

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    ManagedSampler sampler;

    // Tile info buffer for shader (RAII-managed)
    ManagedBuffer tileInfoBuffer_;
    void* tileInfoMappedPtr = nullptr;

    // Tile array texture (sampler2DArray) for shader - holds all active tiles
    VkImage tileArrayImage = VK_NULL_HANDLE;
    VmaAllocation tileArrayAllocation = VK_NULL_HANDLE;
    VkImageView tileArrayView = VK_NULL_HANDLE;

    // Configuration from metadata
    std::string cacheDirectory;
    float terrainSize = 16384.0f;
    float heightScale = 235.0f;
    float minAltitude = -15.0f;
    float maxAltitude = 220.0f;
    uint32_t tileResolution = 512;
    uint32_t numLODLevels = 4;
    uint32_t tilesX = 32;  // LOD0 tile count
    uint32_t tilesZ = 32;
    uint32_t sourceWidth = 16384;
    uint32_t sourceHeight = 16384;

    // All loaded tiles (keyed by coord + LOD)
    std::unordered_map<uint64_t, TerrainTile> loadedTiles;

    // Active tiles for current frame (pointers into loadedTiles)
    std::vector<TerrainTile*> activeTiles;

    // Maximum active tiles (limits GPU memory usage)
    static constexpr uint32_t MAX_ACTIVE_TILES = 64;
};
