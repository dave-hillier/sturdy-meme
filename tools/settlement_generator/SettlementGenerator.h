#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Settlement types (matching BiomeGenerator.h)
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
    float radius;               // Settlement area radius in meters
    float score;
    std::vector<std::string> features;
};

// Biome zone types for settlement placement decisions
enum class BiomeZone : uint8_t {
    Sea = 0,
    Beach = 1,
    ChalkCliff = 2,
    SaltMarsh = 3,
    River = 4,
    Wetland = 5,
    Grassland = 6,
    Agricultural = 7,
    Woodland = 8,
    Count
};

struct SettlementConfig {
    std::string heightmapPath;
    std::string erosionCacheDir;
    std::string biomeMapPath;       // Optional - if not provided, basic classification is used
    std::string outputDir;

    float seaLevel = 0.0f;
    float terrainSize = 16384.0f;
    float minAltitude = 0.0f;
    float maxAltitude = 200.0f;
    uint32_t numSettlements = 20;

    // Settlement distance thresholds
    float hamletMinDistance = 400.0f;
    float villageMinDistance = 800.0f;
    float townMinDistance = 2000.0f;

    // Settlement area radii (in meters)
    float hamletRadius = 50.0f;
    float villageRadius = 100.0f;
    float townRadius = 200.0f;
    float fishingVillageRadius = 80.0f;

    // Zone thresholds for basic classification
    float beachMaxHeight = 3.0f;
    float coastalDistance = 200.0f;
    float riverFlowThreshold = 0.3f;

    // SVG output options
    int svgWidth = 2048;
    int svgHeight = 2048;
};

struct SettlementResult {
    std::vector<Settlement> settlements;

    // Intermediate data for scoring
    std::vector<float> slopeMap;
    std::vector<float> distanceToSea;
    std::vector<float> distanceToRiver;
    uint32_t width;
    uint32_t height;
};

class SettlementGenerator {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    SettlementGenerator() = default;
    ~SettlementGenerator() = default;

    bool generate(const SettlementConfig& config, ProgressCallback callback = nullptr);

    const SettlementResult& getResult() const { return result; }

    // Output functions
    bool saveSettlements(const std::string& path) const;
    bool saveSettlementsSVG(const std::string& path) const;

    static const char* getSettlementTypeName(SettlementType type);

private:
    bool loadHeightmap(const std::string& path, ProgressCallback callback);
    bool loadErosionData(const std::string& cacheDir, ProgressCallback callback);
    bool loadBiomeMap(const std::string& path, ProgressCallback callback);

    void computeSlopeMap(ProgressCallback callback);
    void computeDistanceToSea(ProgressCallback callback);
    void computeDistanceToRiver(ProgressCallback callback);
    void classifyBasicZones(ProgressCallback callback);

    void placeSettlements(ProgressCallback callback);

    float sampleHeight(float x, float z) const;
    float sampleSlope(float x, float z) const;
    float sampleFlowAccumulation(float x, float z) const;
    BiomeZone sampleZone(float x, float z) const;

    float calculateSettlementScore(float x, float z) const;
    bool isValidSettlementLocation(float x, float z, const std::vector<Settlement>& existing) const;

    SettlementConfig config;
    SettlementResult result;

    // Source data
    std::vector<float> heightData;
    uint32_t heightmapWidth = 0;
    uint32_t heightmapHeight = 0;

    std::vector<float> flowAccumulation;
    std::vector<int8_t> flowDirection;
    uint32_t flowMapWidth = 0;
    uint32_t flowMapHeight = 0;

    std::vector<BiomeZone> biomeZones;
    uint32_t biomeMapWidth = 0;
    uint32_t biomeMapHeight = 0;
    bool hasBiomeMap = false;
};
