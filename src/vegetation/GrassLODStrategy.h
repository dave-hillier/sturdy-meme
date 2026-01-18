#pragma once

#include <cstdint>
#include <memory>
#include <string>

/**
 * IGrassLODStrategy - Interface for configurable grass LOD behavior
 *
 * This interface allows different LOD configurations without code changes:
 * - Performance presets (fewer LODs, larger tiles)
 * - Quality presets (more LODs, denser grass)
 * - Custom configurations for specific scenes
 *
 * The strategy controls:
 * - Number and arrangement of LOD levels
 * - Tile sizes and grass density per LOD
 * - LOD transition distances and blend zones
 * - Visual variation parameters (jitter, scale, etc.)
 */
class IGrassLODStrategy {
public:
    virtual ~IGrassLODStrategy() = default;

    // =========================================================================
    // Basic LOD Configuration
    // =========================================================================

    /**
     * Get the number of LOD levels (typically 2-4)
     */
    virtual uint32_t getNumLODLevels() const = 0;

    /**
     * Get the LOD level for a given distance from camera
     * Returns LOD index (0 = highest detail)
     */
    virtual uint32_t getLODForDistance(float distance) const = 0;

    /**
     * Get tile size (meters) for a given LOD level
     */
    virtual float getTileSize(uint32_t lod) const = 0;

    /**
     * Get spacing multiplier for a given LOD level
     * LOD 0 = 1.0, higher LODs have larger multipliers (sparser grass)
     */
    virtual float getSpacingMultiplier(uint32_t lod) const = 0;

    /**
     * Get number of tiles per axis for a given LOD level
     * e.g., 3 means a 3x3 grid of tiles around the camera
     */
    virtual uint32_t getTilesPerAxis(uint32_t lod) const = 0;

    // =========================================================================
    // Distance Thresholds
    // =========================================================================

    /**
     * Get the distance where a LOD level ends
     */
    virtual float getLODEndDistance(uint32_t lod) const = 0;

    /**
     * Get maximum draw distance (beyond which no grass renders)
     */
    virtual float getMaxDrawDistance() const = 0;

    /**
     * Get LOD transition zone size (for smooth blending)
     */
    virtual float getTransitionZoneSize() const = 0;

    /**
     * Get transition drop rate (how quickly grass fades in transition)
     */
    virtual float getTransitionDropRate() const = 0;

    // =========================================================================
    // Visual Variation (to reduce regularity/popping)
    // =========================================================================

    /**
     * Get position jitter factor for a LOD level
     * Higher values = more randomness in blade positions
     * Default ~0.8, increase to reduce grid appearance
     */
    virtual float getJitterFactor(uint32_t lod) const = 0;

    /**
     * Get height variation range for a LOD level
     * Returns (minScale, maxScale) multipliers for blade height
     */
    virtual void getHeightVariation(uint32_t lod, float& minScale, float& maxScale) const = 0;

    /**
     * Get width variation range for a LOD level
     * Returns (minScale, maxScale) multipliers for blade width
     */
    virtual void getWidthVariation(uint32_t lod, float& minScale, float& maxScale) const = 0;

    /**
     * Get hysteresis amount for LOD transitions
     * Higher = less popping but slower response to camera movement
     */
    virtual float getLODHysteresis() const = 0;

    // =========================================================================
    // Tile Streaming
    // =========================================================================

    /**
     * Get load margin (extra distance to start loading tiles)
     */
    virtual float getTileLoadMargin() const = 0;

    /**
     * Get unload margin (extra distance before unloading tiles)
     */
    virtual float getTileUnloadMargin() const = 0;

    /**
     * Get tile fade-in duration (seconds)
     */
    virtual float getTileFadeInDuration() const = 0;

    // =========================================================================
    // Strategy Info
    // =========================================================================

    /**
     * Get strategy name for logging/UI
     */
    virtual const std::string& getName() const = 0;

    /**
     * Get strategy description
     */
    virtual const std::string& getDescription() const = 0;
};

/**
 * Create default LOD strategy (matches current GrassConstants)
 */
std::unique_ptr<IGrassLODStrategy> createDefaultGrassLODStrategy();

/**
 * Create performance-focused strategy (fewer LODs, sparser grass)
 */
std::unique_ptr<IGrassLODStrategy> createPerformanceGrassLODStrategy();

/**
 * Create quality-focused strategy (more LODs, denser grass, longer draw distance)
 */
std::unique_ptr<IGrassLODStrategy> createQualityGrassLODStrategy();

/**
 * Create ultra quality strategy (maximum detail, large transition zones)
 */
std::unique_ptr<IGrassLODStrategy> createUltraGrassLODStrategy();
