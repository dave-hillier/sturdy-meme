#pragma once

#include "MaterialLibrary.h"
#include "SplineRasterizer.h"
#include "../road_generator/RoadSpline.h"
#include "../../src/BiomeGenerator.h"
#include "../../src/ErosionSimulator.h"
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>

namespace VirtualTexture {

// Configuration for the tile compositor
struct TileCompositorConfig {
    float terrainSize = 16384.0f;       // World terrain size in meters
    float minAltitude = 0.0f;           // Minimum heightmap altitude
    float maxAltitude = 200.0f;         // Maximum heightmap altitude

    uint32_t tileResolution = 128;       // Pixels per tile
    uint32_t tilesPerAxis = 512;         // Number of tiles per axis at mip 0
    uint32_t maxMipLevels = 9;           // log2(512) = 9

    // Material sampling
    float materialTilingScale = 4.0f;    // Base UV tiling for materials
    float slopeThreshold = 0.5f;         // Slope for cliff material blend start
    float slopeBlendRange = 0.3f;        // Range over which cliff blend occurs

    // Noise settings for sub-zone variation
    float subZoneNoiseScale = 0.01f;     // Noise frequency for sub-zone blending
    float subZoneBlendStrength = 0.3f;   // Max blend amount for sub-zones

    // Get tile size in world units
    float getTileSize() const {
        return terrainSize / tilesPerAxis;
    }

    // Get number of tiles at a given mip level
    uint32_t getTilesAtMip(uint32_t mipLevel) const {
        return tilesPerAxis >> mipLevel;
    }
};

// Loaded texture data (simple RGBA8)
struct TextureData {
    std::vector<uint8_t> pixels;    // RGBA8 data
    uint32_t width = 0;
    uint32_t height = 0;

    bool isValid() const { return !pixels.empty() && width > 0 && height > 0; }

    // Sample with bilinear interpolation, returns color in [0,1]
    glm::vec4 sample(glm::vec2 uv) const;

    // Sample with wrapping
    glm::vec4 sampleWrap(glm::vec2 uv) const;
};

// Heightmap data
struct HeightmapData {
    std::vector<float> heights;     // Normalized [0,1] heights
    uint32_t width = 0;
    uint32_t height = 0;

    bool isValid() const { return !heights.empty() && width > 0 && height > 0; }

    // Sample height at world position
    float sampleHeight(float x, float z, float terrainSize) const;

    // Get slope magnitude at world position
    float sampleSlope(float x, float z, float terrainSize) const;

    // Get surface normal at world position
    glm::vec3 sampleNormal(float x, float z, float terrainSize) const;
};

// Biome map data
struct BiomeMapData {
    std::vector<uint8_t> zones;     // BiomeZone values
    std::vector<uint8_t> subZones;  // Sub-zone variations
    uint32_t width = 0;
    uint32_t height = 0;

    bool isValid() const { return !zones.empty() && width > 0 && height > 0; }

    // Sample biome at world position
    BiomeZone sampleZone(float x, float z, float terrainSize) const;
    uint8_t sampleSubZone(float x, float z, float terrainSize) const;
};

// Cache of loaded material textures
struct MaterialTextureCache {
    std::unordered_map<std::string, TextureData> textures;

    // Load a texture if not already cached
    const TextureData* getTexture(const std::string& path);

    // Clear the cache
    void clear() { textures.clear(); }
};

// Output tile data
struct OutputTile {
    std::vector<uint8_t> pixels;    // RGBA8 data
    uint32_t tileX = 0;
    uint32_t tileY = 0;
    uint32_t mipLevel = 0;
    uint32_t resolution = 128;

    // Get pixel index
    size_t pixelIndex(uint32_t x, uint32_t y) const {
        return (y * resolution + x) * 4;
    }

    // Set pixel color
    void setPixel(uint32_t x, uint32_t y, glm::vec4 color);

    // Get pixel color
    glm::vec4 getPixel(uint32_t x, uint32_t y) const;
};

// Main tile compositor class
class TileCompositor {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    TileCompositor();
    ~TileCompositor() = default;

    // Initialize with configuration
    void init(const TileCompositorConfig& config);

    // Load required data
    bool loadHeightmap(const std::string& path);
    bool loadBiomeMap(const std::string& path);
    bool loadRoads(const std::string& jsonPath);
    bool loadRivers(const std::string& erosionCachePath);

    // Set material library base path
    void setMaterialBasePath(const std::string& path);

    // Generate a single tile at specified mip level
    void generateTile(uint32_t tileX, uint32_t tileY, uint32_t mipLevel, OutputTile& outTile);

    // Generate all tiles at a mip level to output directory
    bool generateMipLevel(uint32_t mipLevel, const std::string& outputDir,
                          ProgressCallback callback = nullptr);

    // Generate complete mip chain
    bool generateAllMips(const std::string& outputDir,
                         ProgressCallback callback = nullptr);

    // Save metadata JSON
    bool saveMetadata(const std::string& outputDir) const;

    // Get statistics
    size_t getLoadedTextureCount() const { return textureCache.textures.size(); }

private:
    // Sample the base terrain color at a world position
    glm::vec4 sampleBaseTerrain(glm::vec2 worldPos);

    // Sample a material texture with triplanar mapping
    glm::vec4 sampleMaterialTriplanar(const TerrainMaterial& material,
                                       glm::vec2 worldPos, glm::vec3 normal);

    // Simple 2D noise for sub-zone variation
    float noise2D(glm::vec2 pos) const;

    // Smoothstep function
    static float smoothstep(float edge0, float edge1, float x);

    TileCompositorConfig config;
    MaterialLibrary materials;
    SplineRasterizer splineRasterizer;

    HeightmapData heightmap;
    BiomeMapData biomeMap;
    MaterialTextureCache textureCache;

    std::string materialBasePath;
    bool dataLoaded = false;
};

} // namespace VirtualTexture
