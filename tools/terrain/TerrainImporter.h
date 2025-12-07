#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// Configuration for terrain import
struct TerrainImportConfig {
    std::string sourceHeightmapPath;  // Path to source 16-bit PNG
    std::string cacheDirectory;        // Directory for tile cache

    float minAltitude = 0.0f;          // Altitude in meters for height value 0
    float maxAltitude = 200.0f;        // Altitude in meters for height value 65535
    float metersPerPixel = 1.0f;       // World scale (meters per pixel)

    uint32_t tileResolution = 512;     // Output tile resolution (512x512)
    uint32_t numLODLevels = 4;         // Number of LOD levels to generate
};

// Progress callback for import operation
using ImportProgressCallback = std::function<void(float progress, const std::string& status)>;

// Terrain importer - loads source heightmap and generates tile cache
class TerrainImporter {
public:
    TerrainImporter() = default;
    ~TerrainImporter();

    // Check if cache exists and is valid for the given config
    bool isCacheValid(const TerrainImportConfig& config) const;

    // Import source heightmap and generate tile cache
    // Returns true on success
    bool import(const TerrainImportConfig& config, ImportProgressCallback progressCallback = nullptr);

    // Get terrain dimensions after import (in tiles)
    uint32_t getTilesX() const { return tilesX; }
    uint32_t getTilesZ() const { return tilesZ; }

    // Get source image dimensions
    uint32_t getSourceWidth() const { return sourceWidth; }
    uint32_t getSourceHeight() const { return sourceHeight; }

    // Get world dimensions (in meters)
    float getWorldWidth() const { return worldWidth; }
    float getWorldHeight() const { return worldHeight; }

    // Get the path to a cached tile (16-bit PNG format)
    static std::string getTilePath(const std::string& cacheDir, int32_t x, int32_t z, uint32_t lod);

    // Get metadata path
    static std::string getMetadataPath(const std::string& cacheDir);

    // Calculate number of tiles at a given LOD level
    // LOD 0 uses full resolution, each subsequent LOD has half the source pixels
    static void getTileCountForLOD(uint32_t sourceWidth, uint32_t sourceHeight,
                                   uint32_t tileResolution, uint32_t lod,
                                   uint32_t& outTilesX, uint32_t& outTilesZ);

private:
    // Load 16-bit PNG heightmap
    bool loadSourceHeightmap(const std::string& path);

    // Generate tiles for a specific LOD level
    bool generateLODLevel(const TerrainImportConfig& config, uint32_t lod,
                          ImportProgressCallback progressCallback, float progressBase, float progressRange);

    // Downsample source data for LOD generation
    void downsampleForLOD(uint32_t lod);

    // Save a single tile as 16-bit grayscale PNG
    bool saveTile(const std::string& path, const std::vector<uint16_t>& data, uint32_t resolution);

    // Save/load cache metadata
    bool saveMetadata(const TerrainImportConfig& config) const;
    bool loadAndValidateMetadata(const TerrainImportConfig& config) const;

    // Source heightmap data (16-bit)
    std::vector<uint16_t> sourceData;
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;

    // Current LOD working data
    std::vector<uint16_t> lodData;
    uint32_t lodWidth = 0;
    uint32_t lodHeight = 0;

    // Calculated dimensions
    uint32_t tilesX = 0;
    uint32_t tilesZ = 0;
    float worldWidth = 0.0f;
    float worldHeight = 0.0f;
};
