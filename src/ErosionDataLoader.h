#pragma once

// Lightweight erosion data loader for runtime use
// Only loads pre-computed data from cache - no simulation capability
// Simulation is done by the erosion_preprocess tool during build

#include "WaterPlacementData.h"
#include <string>
#include <cstdint>

// Configuration for loading cached erosion data
struct ErosionLoadConfig {
    std::string sourceHeightmapPath;       // Path to source heightmap (for cache validation)
    std::string cacheDirectory;            // Directory containing cached results
    float seaLevel = 0.0f;                 // Sea level threshold
};

// Erosion data loader - loads pre-computed water placement data from cache
class ErosionDataLoader {
public:
    ErosionDataLoader() = default;
    ~ErosionDataLoader() = default;

    // Check if cached results exist and are valid
    bool isCacheValid(const ErosionLoadConfig& config) const;

    // Load cached results (call after isCacheValid returns true)
    bool loadFromCache(const ErosionLoadConfig& config);

    // Get the loaded water placement data
    const WaterPlacementData& getWaterData() const { return waterData; }
    WaterPlacementData& getWaterData() { return waterData; }

    // Get cache file paths (static, for tools to know where to write)
    static std::string getFlowMapPath(const std::string& cacheDir);
    static std::string getRiversPath(const std::string& cacheDir);
    static std::string getLakesPath(const std::string& cacheDir);
    static std::string getMetadataPath(const std::string& cacheDir);

private:
    bool loadAndValidateMetadata(const ErosionLoadConfig& config) const;

    WaterPlacementData waterData;
};
