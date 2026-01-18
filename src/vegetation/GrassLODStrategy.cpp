#include "GrassLODStrategy.h"
#include "GrassConstants.h"
#include <cmath>
#include <algorithm>
#include <vector>

/**
 * ConfigurableGrassLODStrategy - Configurable implementation of IGrassLODStrategy
 * (Not in anonymous namespace to allow unique_ptr conversion to base class)
 */
class ConfigurableGrassLODStrategy : public IGrassLODStrategy {
public:
    struct LODLevel {
        float tileSize;
        float spacingMult;
        uint32_t tilesPerAxis;
        float endDistance;
        float jitterFactor;
        float heightMin;
        float heightMax;
        float widthMin;
        float widthMax;
    };

    struct Config {
        std::string name;
        std::string description;
        std::vector<LODLevel> levels;
        float transitionZone;
        float transitionDropRate;
        float lodHysteresis;
        float tileLoadMargin;
        float tileUnloadMargin;
        float tileFadeInDuration;
        float maxDrawDistance;
    };

    explicit ConfigurableGrassLODStrategy(Config config)
        : config_(std::move(config)) {}

    uint32_t getNumLODLevels() const override {
        return static_cast<uint32_t>(config_.levels.size());
    }

    uint32_t getLODForDistance(float distance) const override {
        for (uint32_t lod = 0; lod < config_.levels.size(); ++lod) {
            if (distance <= config_.levels[lod].endDistance) {
                return lod;
            }
        }
        return static_cast<uint32_t>(config_.levels.size() - 1);
    }

    float getTileSize(uint32_t lod) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        return config_.levels[lod].tileSize;
    }

    float getSpacingMultiplier(uint32_t lod) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        return config_.levels[lod].spacingMult;
    }

    uint32_t getTilesPerAxis(uint32_t lod) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        return config_.levels[lod].tilesPerAxis;
    }

    float getLODEndDistance(uint32_t lod) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        return config_.levels[lod].endDistance;
    }

    float getMaxDrawDistance() const override {
        return config_.maxDrawDistance;
    }

    float getTransitionZoneSize() const override {
        return config_.transitionZone;
    }

    float getTransitionDropRate() const override {
        return config_.transitionDropRate;
    }

    float getJitterFactor(uint32_t lod) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        return config_.levels[lod].jitterFactor;
    }

    void getHeightVariation(uint32_t lod, float& minScale, float& maxScale) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        minScale = config_.levels[lod].heightMin;
        maxScale = config_.levels[lod].heightMax;
    }

    void getWidthVariation(uint32_t lod, float& minScale, float& maxScale) const override {
        if (lod >= config_.levels.size()) lod = static_cast<uint32_t>(config_.levels.size() - 1);
        minScale = config_.levels[lod].widthMin;
        maxScale = config_.levels[lod].widthMax;
    }

    float getLODHysteresis() const override {
        return config_.lodHysteresis;
    }

    float getTileLoadMargin() const override {
        return config_.tileLoadMargin;
    }

    float getTileUnloadMargin() const override {
        return config_.tileUnloadMargin;
    }

    float getTileFadeInDuration() const override {
        return config_.tileFadeInDuration;
    }

    const std::string& getName() const override {
        return config_.name;
    }

    const std::string& getDescription() const override {
        return config_.description;
    }

private:
    Config config_;
};

std::unique_ptr<IGrassLODStrategy> createDefaultGrassLODStrategy() {
    ConfigurableGrassLODStrategy::Config config;
    config.name = "Default";
    config.description = "Balanced quality and performance (matches original constants)";

    // Base tile size from constants
    float baseTileSize = GrassConstants::TILE_SIZE_LOD0;

    // LOD 0: High detail
    config.levels.push_back({
        baseTileSize,           // tileSize
        1.0f,                   // spacingMult
        3,                      // tilesPerAxis (3x3)
        GrassConstants::LOD0_DISTANCE_END,
        0.85f,                  // jitterFactor (slightly higher than default 0.8)
        0.85f, 1.15f,           // height variation
        0.9f, 1.1f              // width variation
    });

    // LOD 1: Medium detail
    config.levels.push_back({
        baseTileSize * 2.0f,
        2.0f,
        3,
        GrassConstants::LOD1_DISTANCE_END,
        0.9f,                   // More jitter at distance
        0.8f, 1.2f,
        0.85f, 1.15f
    });

    // LOD 2: Low detail
    config.levels.push_back({
        baseTileSize * 4.0f,
        4.0f,
        3,
        GrassConstants::MAX_DRAW_DISTANCE,
        0.95f,                  // Maximum jitter at far distance
        0.75f, 1.25f,
        0.8f, 1.2f
    });

    config.transitionZone = GrassConstants::LOD_TRANSITION_ZONE;
    config.transitionDropRate = GrassConstants::LOD_TRANSITION_DROP_RATE;
    config.lodHysteresis = GrassConstants::GRASS_LOD_HYSTERESIS;
    config.tileLoadMargin = GrassConstants::TILE_LOAD_MARGIN;
    config.tileUnloadMargin = GrassConstants::TILE_UNLOAD_MARGIN;
    config.tileFadeInDuration = GrassConstants::GRASS_TILE_FADE_IN_DURATION;
    config.maxDrawDistance = GrassConstants::MAX_DRAW_DISTANCE;

    return std::make_unique<ConfigurableGrassLODStrategy>(std::move(config));
}

std::unique_ptr<IGrassLODStrategy> createPerformanceGrassLODStrategy() {
    ConfigurableGrassLODStrategy::Config config;
    config.name = "Performance";
    config.description = "Optimized for performance (2 LODs, sparser grass, shorter draw)";

    float baseTileSize = GrassConstants::TILE_SIZE_LOD0 * 1.5f;  // Larger tiles

    // LOD 0: High detail (smaller area)
    config.levels.push_back({
        baseTileSize,
        1.5f,                   // Sparser grass
        2,                      // 2x2 tiles only
        40.0f,                  // Shorter LOD 0 range
        0.9f,
        0.85f, 1.15f,
        0.9f, 1.1f
    });

    // LOD 1: Low detail
    config.levels.push_back({
        baseTileSize * 3.0f,
        4.0f,                   // Much sparser
        2,
        100.0f,
        1.0f,                   // Maximum jitter
        0.7f, 1.3f,
        0.75f, 1.25f
    });

    config.transitionZone = 15.0f;  // Larger transition zone
    config.transitionDropRate = 0.6f;
    config.lodHysteresis = 0.15f;
    config.tileLoadMargin = 15.0f;
    config.tileUnloadMargin = 30.0f;
    config.tileFadeInDuration = 0.5f;
    config.maxDrawDistance = 100.0f;

    return std::make_unique<ConfigurableGrassLODStrategy>(std::move(config));
}

std::unique_ptr<IGrassLODStrategy> createQualityGrassLODStrategy() {
    ConfigurableGrassLODStrategy::Config config;
    config.name = "Quality";
    config.description = "High quality (3 LODs, denser grass, longer draw, smooth transitions)";

    float baseTileSize = GrassConstants::TILE_SIZE_LOD0 * 0.8f;  // Smaller tiles for finer detail

    // LOD 0: Very high detail
    config.levels.push_back({
        baseTileSize,
        0.8f,                   // Denser grass
        5,                      // 5x5 tiles
        60.0f,
        0.9f,
        0.9f, 1.1f,
        0.95f, 1.05f
    });

    // LOD 1: High detail
    config.levels.push_back({
        baseTileSize * 2.0f,
        1.5f,
        5,
        120.0f,
        0.92f,
        0.85f, 1.15f,
        0.9f, 1.1f
    });

    // LOD 2: Medium detail
    config.levels.push_back({
        baseTileSize * 4.0f,
        3.0f,
        5,
        200.0f,
        0.95f,
        0.8f, 1.2f,
        0.85f, 1.15f
    });

    config.transitionZone = 20.0f;  // Larger transition zones
    config.transitionDropRate = 0.85f;
    config.lodHysteresis = 0.08f;  // Lower hysteresis for quicker response
    config.tileLoadMargin = 20.0f;
    config.tileUnloadMargin = 35.0f;
    config.tileFadeInDuration = 1.0f;  // Longer fade
    config.maxDrawDistance = 200.0f;

    return std::make_unique<ConfigurableGrassLODStrategy>(std::move(config));
}

std::unique_ptr<IGrassLODStrategy> createUltraGrassLODStrategy() {
    ConfigurableGrassLODStrategy::Config config;
    config.name = "Ultra";
    config.description = "Maximum quality (4 LODs, highest density, very smooth transitions)";

    float baseTileSize = GrassConstants::TILE_SIZE_LOD0 * 0.6f;  // Much smaller tiles

    // LOD 0: Ultra detail
    config.levels.push_back({
        baseTileSize,
        0.6f,                   // Very dense grass
        7,                      // 7x7 tiles
        45.0f,
        0.92f,
        0.92f, 1.08f,
        0.96f, 1.04f
    });

    // LOD 1: Very high detail
    config.levels.push_back({
        baseTileSize * 1.5f,
        1.0f,
        5,
        90.0f,
        0.94f,
        0.9f, 1.1f,
        0.94f, 1.06f
    });

    // LOD 2: High detail
    config.levels.push_back({
        baseTileSize * 3.0f,
        2.0f,
        5,
        150.0f,
        0.95f,
        0.85f, 1.15f,
        0.9f, 1.1f
    });

    // LOD 3: Medium detail (far distance)
    config.levels.push_back({
        baseTileSize * 6.0f,
        4.0f,
        3,
        300.0f,
        0.98f,
        0.8f, 1.2f,
        0.85f, 1.15f
    });

    config.transitionZone = 25.0f;  // Very large transition zones
    config.transitionDropRate = 0.9f;
    config.lodHysteresis = 0.05f;  // Minimal hysteresis
    config.tileLoadMargin = 25.0f;
    config.tileUnloadMargin = 50.0f;
    config.tileFadeInDuration = 1.5f;  // Very smooth fade
    config.maxDrawDistance = 300.0f;

    return std::make_unique<ConfigurableGrassLODStrategy>(std::move(config));
}
