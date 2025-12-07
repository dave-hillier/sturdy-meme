#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

// Zone types for the south coast of England
enum class BiomeZone : uint8_t {
    Sea = 0,
    Beach = 1,
    ChalkCliff = 2,
    SaltMarsh = 3,
    River = 4,
    Wetland = 5,
    Grassland = 6,      // Chalk downs
    Agricultural = 7,
    Woodland = 8,

    Count
};

// Sub-zone variations within each major zone
enum class BiomeSubZone : uint8_t {
    // Grassland sub-zones
    OpenDown = 0,
    WildflowerMeadow = 1,
    GorsePatch = 2,
    ChalkScrape = 3,

    // Woodland sub-zones
    BeechFloor = 0,
    OakUnderstorey = 1,
    Clearing = 2,
    Coppice = 3,

    // Agricultural sub-zones
    Ploughed = 0,
    Pasture = 1,
    CropField = 2,
    Fallow = 3,

    // Salt marsh sub-zones
    Mudflat = 0,
    Saltpan = 1,
    Cordgrass = 2,
    Creek = 3,

    // Default
    Default = 0
};

// Settlement types
enum class SettlementType : uint8_t {
    Hamlet = 0,
    Village = 1,
    Town = 2,
    FishingVillage = 3
};

struct Settlement {
    uint32_t id;
    SettlementType type;
    glm::vec2 position;         // World coordinates
    float score;
    std::vector<std::string> features;
};

struct BiomeConfig {
    std::string heightmapPath;
    std::string erosionCacheDir;
    std::string outputDir;

    float seaLevel = 0.0f;
    float terrainSize = 16384.0f;
    float minAltitude = 0.0f;
    float maxAltitude = 200.0f;
    uint32_t outputResolution = 1024;
    uint32_t numSettlements = 20;

    // Zone thresholds
    float cliffSlopeThreshold = 0.7f;
    float beachMaxHeight = 3.0f;
    float beachMaxSlope = 0.1f;
    float marshMaxHeight = 8.0f;
    float marshMaxSlope = 0.15f;
    float grasslandMinHeight = 50.0f;
    float grasslandMaxSlope = 0.3f;
    float agriculturalMaxSlope = 0.2f;
    float agriculturalMinHeight = 10.0f;
    float agriculturalMaxHeight = 80.0f;
    float coastalDistance = 200.0f;
    float riverFlowThreshold = 0.3f;
    float wetlandRiverDistance = 100.0f;

    // Settlement thresholds
    float hamletMinDistance = 400.0f;
    float villageMinDistance = 800.0f;
    float townMinDistance = 2000.0f;
};

struct BiomeCell {
    BiomeZone zone;
    BiomeSubZone subZone;
    float distanceToSettlement;
    uint8_t reserved;
};

struct BiomeResult {
    std::vector<BiomeCell> cells;
    std::vector<Settlement> settlements;
    uint32_t width;
    uint32_t height;

    // Intermediate data (for debugging)
    std::vector<float> slopeMap;
    std::vector<float> distanceToSea;
    std::vector<float> distanceToRiver;
};

class BiomeGenerator {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    BiomeGenerator() = default;
    ~BiomeGenerator() = default;

    bool generate(const BiomeConfig& config, ProgressCallback callback = nullptr);

    const BiomeResult& getResult() const { return result; }

    // Output functions
    bool saveBiomeMap(const std::string& path) const;
    bool saveDebugVisualization(const std::string& path) const;
    bool saveSettlements(const std::string& path) const;

    // Zone color for visualization
    static glm::vec3 getZoneColor(BiomeZone zone);
    static const char* getZoneName(BiomeZone zone);
    static const char* getSettlementTypeName(SettlementType type);

private:
    bool loadHeightmap(const std::string& path, ProgressCallback callback);
    bool loadErosionData(const std::string& cacheDir, ProgressCallback callback);

    void computeSlopeMap(ProgressCallback callback);
    void computeDistanceToSea(ProgressCallback callback);
    void computeDistanceToRiver(ProgressCallback callback);

    void classifyZones(ProgressCallback callback);
    void applySubZoneNoise(ProgressCallback callback);
    void placeSettlements(ProgressCallback callback);
    void computeSettlementDistances(ProgressCallback callback);

    float sampleHeight(float x, float z) const;
    float sampleSlope(float x, float z) const;
    float sampleFlowAccumulation(float x, float z) const;
    int8_t sampleFlowDirection(float x, float z) const;

    float calculateSettlementScore(float x, float z) const;
    bool isValidSettlementLocation(float x, float z, const std::vector<Settlement>& existing) const;

    // Noise function for sub-zone variation
    float noise2D(float x, float y, float frequency) const;

    BiomeConfig config;
    BiomeResult result;

    // Source data
    std::vector<float> heightData;
    uint32_t heightmapWidth = 0;
    uint32_t heightmapHeight = 0;

    std::vector<float> flowAccumulation;
    std::vector<int8_t> flowDirection;
    uint32_t flowMapWidth = 0;
    uint32_t flowMapHeight = 0;
};
