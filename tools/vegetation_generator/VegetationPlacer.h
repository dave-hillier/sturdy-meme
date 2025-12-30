#pragma once

#include "VegetationConfig.h"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <random>
#include <unordered_map>
#include <string>

/**
 * Vegetation placement using Poisson disk sampling.
 *
 * Uses a fast Poisson disk algorithm (Bridson's algorithm) to generate
 * natural-looking distributions with guaranteed minimum spacing.
 *
 * Features:
 * - Tile-based generation for streaming/paging
 * - Biome-aware density variation
 * - Deterministic seeding for reproducibility
 * - Multi-layer placement (trees, bushes, rocks, detritus)
 */
class VegetationPlacer {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    VegetationPlacer() = default;
    ~VegetationPlacer() = default;

    /**
     * Generate vegetation placement for the entire terrain.
     * Results are stored as tiles for efficient paging.
     */
    bool generate(const VegetationGeneratorConfig& config, ProgressCallback callback = nullptr);

    /**
     * Generate placement for a single tile (for incremental/streaming generation).
     */
    bool generateTile(int32_t tileX, int32_t tileZ, const VegetationGeneratorConfig& config,
                      VegetationTile& outTile);

    // Access results
    const std::vector<VegetationTile>& getTiles() const { return tiles_; }
    size_t getTotalInstanceCount() const;

    // Output
    bool saveTiles(const std::string& outputDir) const;
    bool saveSVG(const std::string& path, int size) const;
    bool saveManifest(const std::string& path) const;

    // Statistics
    struct Statistics {
        size_t totalTrees = 0;
        size_t totalBushes = 0;
        size_t totalRocks = 0;
        size_t totalDetritus = 0;
        size_t tilesGenerated = 0;
        std::unordered_map<std::string, size_t> byType;
    };
    const Statistics& getStatistics() const { return stats_; }

private:
    // Poisson disk sampling using Bridson's algorithm
    struct PoissonDisk {
        std::vector<glm::vec2> points;
        float minDistance;
        float cellSize;
        int gridWidth;
        int gridHeight;
        std::vector<int> grid;  // -1 for empty, index otherwise

        void init(float areaWidth, float areaHeight, float minDist);
        bool addPoint(const glm::vec2& p);
        bool isValid(const glm::vec2& p) const;
        int getGridIndex(const glm::vec2& p) const;
    };

    /**
     * Generate points using Poisson disk sampling.
     *
     * @param bounds    Area bounds (min, max)
     * @param minDist   Minimum distance between points
     * @param density   Target density (points per square unit)
     * @param rng       Random number generator
     * @param outPoints Generated points
     */
    void poissonDiskSample(
        const glm::vec2& boundsMin,
        const glm::vec2& boundsMax,
        float minDist,
        float density,
        std::mt19937& rng,
        std::vector<glm::vec2>& outPoints
    );

    /**
     * Select vegetation type based on biome and probabilities.
     */
    VegetationType selectTreeType(BiomeZone biome, std::mt19937& rng);
    VegetationType selectBushType(std::mt19937& rng);
    VegetationType selectDetritusType(std::mt19937& rng);

    /**
     * Get tree size variant (small/medium/large) based on random selection.
     */
    VegetationType selectTreeSize(VegetationType baseType, std::mt19937& rng);

    /**
     * Get density configuration for a biome.
     */
    const BiomeDensityConfig& getDensityForBiome(BiomeZone biome) const;

    /**
     * Load and query biome map.
     */
    bool loadBiomeMap(const std::string& path);
    BiomeZone getBiomeAt(float worldX, float worldZ) const;

    /**
     * Load heightmap for slope-based filtering.
     */
    bool loadHeightmap(const std::string& path);
    float getHeightAt(float worldX, float worldZ) const;
    float getSlopeAt(float worldX, float worldZ) const;

    // Configuration (stored during generate)
    VegetationGeneratorConfig config_;

    // Generated tiles
    std::vector<VegetationTile> tiles_;

    // Biome map data
    std::vector<uint8_t> biomeData_;
    uint32_t biomeWidth_ = 0;
    uint32_t biomeHeight_ = 0;

    // Heightmap data
    std::vector<float> heightData_;
    uint32_t heightWidth_ = 0;
    uint32_t heightHeight_ = 0;

    // Statistics
    Statistics stats_;
};
