#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include "VulkanRAII.h"
#include "core/FrameBuffered.h"

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

    // Index in the tile array texture (-1 = not yet uploaded to array)
    int32_t arrayLayerIndex = -1;
};

// Tile info for GPU (matches shader buffer layout)
struct TileInfoGPU {
    glm::vec4 worldBounds;  // xy = min corner, zw = max corner
    glm::vec4 uvScaleOffset; // xy = scale, zw = offset (for UV calculation)
    glm::ivec4 layerIndex;  // x = layer index in tile array, yzw = padding (std140 alignment)
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

    /**
     * Factory: Create and initialize TerrainTileCache.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainTileCache> create(const InitInfo& info);

    ~TerrainTileCache();

    // Non-copyable, non-movable
    TerrainTileCache(const TerrainTileCache&) = delete;
    TerrainTileCache& operator=(const TerrainTileCache&) = delete;
    TerrainTileCache(TerrainTileCache&&) = delete;
    TerrainTileCache& operator=(TerrainTileCache&&) = delete;

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

    // Get tile info buffer for shader (triple-buffered for frames-in-flight sync)
    // IMPORTANT: Always use this per-frame version to avoid CPU-GPU sync issues.
    // The buffer is written by CPU during updateActiveTiles() and read by GPU shaders.
    // Using the wrong frame's buffer causes flickering artifacts.
    VkBuffer getTileInfoBuffer(uint32_t frameIndex) const {
        return tileInfoBuffers_.at(frameIndex).get();
    }

    // Update which frame we're writing to (call during updateActiveTiles)
    void setCurrentFrameIndex(uint32_t frameIndex) { currentFrameIndex_ = frameIndex; }

    // Number of frames in flight (matches Renderer::MAX_FRAMES_IN_FLIGHT)
    static constexpr uint32_t FRAMES_IN_FLIGHT = TripleBuffered<int>::DEFAULT_FRAME_COUNT;

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

    // Pre-load tiles (CPU data only) around a world position for immediate height queries
    // Call this before spawning objects to ensure getHeightAt() returns high-res values
    void preloadTilesAround(float worldX, float worldZ, float radius);

    // Load all tiles at the coarsest LOD level (LOD3) synchronously at startup
    // These tiles cover the entire terrain and are never unloaded
    // Returns true if all base tiles loaded successfully
    bool loadBaseLODTiles();

    // Check if base LOD tiles are loaded
    bool hasBaseLODTiles() const { return !baseTiles_.empty(); }

    // Get base heightmap texture (combined from LOD3 tiles) for GPU fallback
    VkImageView getBaseHeightMapView() const { return baseHeightMapView_; }
    VkSampler getBaseHeightMapSampler() const { return sampler.get(); }

    // Get base heightmap CPU data for fallback height queries
    const std::vector<float>& getBaseHeightMapData() const { return baseHeightMapCpuData_; }
    uint32_t getBaseHeightMapResolution() const { return baseHeightMapResolution_; }

    // LOD distance thresholds (can be configured)
    static constexpr float LOD0_MAX_DISTANCE = 1000.0f;  // < 1km: LOD0
    static constexpr float LOD1_MAX_DISTANCE = 2000.0f;  // 1-2km: LOD1
    static constexpr float LOD2_MAX_DISTANCE = 4000.0f;  // 2-4km: LOD2
    static constexpr float LOD3_MAX_DISTANCE = 8000.0f;  // 4-8km: LOD3

private:
    TerrainTileCache() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

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

    // Tile info buffers for shader (RAII-managed, triple-buffered for frames-in-flight)
    TripleBuffered<ManagedBuffer> tileInfoBuffers_;
    std::array<void*, FRAMES_IN_FLIGHT> tileInfoMappedPtrs_ = {};  // Raw pointers, no lifecycle management
    uint32_t currentFrameIndex_ = 0;

    // Tile array texture (sampler2DArray) for shader - holds all active tiles
    VkImage tileArrayImage = VK_NULL_HANDLE;
    VmaAllocation tileArrayAllocation = VK_NULL_HANDLE;
    VkImageView tileArrayView = VK_NULL_HANDLE;

    // Configuration from metadata
    std::string cacheDirectory;
    float terrainSize = 16384.0f;
    float heightScale = 0.0f;
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

    // Track which array layers are free (true = free, false = occupied)
    std::array<bool, MAX_ACTIVE_TILES> freeArrayLayers_;

    // Allocate a free layer in the tile array texture, returns -1 if none available
    int32_t allocateArrayLayer();

    // Free a layer in the tile array texture
    void freeArrayLayer(int32_t layerIndex);

    // Base LOD tiles (coarsest level, covering entire terrain, never unloaded)
    std::vector<TerrainTile*> baseTiles_;
    uint32_t baseLOD_ = 0;  // The LOD level used for base tiles (typically numLODLevels - 1)

    // Combined base heightmap (created from base LOD tiles)
    VkImage baseHeightMapImage_ = VK_NULL_HANDLE;
    VmaAllocation baseHeightMapAllocation_ = VK_NULL_HANDLE;
    VkImageView baseHeightMapView_ = VK_NULL_HANDLE;
    std::vector<float> baseHeightMapCpuData_;
    uint32_t baseHeightMapResolution_ = 512;  // Combined base heightmap resolution

    // Create combined base heightmap from base LOD tiles
    bool createBaseHeightMap();

    // Sample height from base LOD tiles (fallback when no high-res tile covers position)
    bool sampleBaseLOD(float worldX, float worldZ, float& outHeight) const;
};
