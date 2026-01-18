#pragma once

#include <cstdint>
#include <string>

class IGrassLODStrategy;

/**
 * Interface for grass system controls.
 * Used by GuiGrassTab to configure grass rendering and LOD.
 */
class IGrassControl {
public:
    virtual ~IGrassControl() = default;

    // LOD strategy presets
    enum class LODPreset {
        Default,
        Performance,
        Quality,
        Ultra
    };

    // Get current LOD preset (may be Custom if modified)
    virtual LODPreset getLODPreset() const = 0;

    // Set LOD preset
    virtual void setLODPreset(LODPreset preset) = 0;

    // Get current LOD strategy name
    virtual std::string getLODStrategyName() const = 0;

    // Statistics
    virtual uint32_t getActiveTileCount() const = 0;
    virtual uint32_t getPendingLoadCount() const = 0;
    virtual uint32_t getTotalLoadedTiles() const = 0;
    virtual uint32_t getActiveTileCountAtLOD(uint32_t lod) const = 0;

    // Debug visualization
    virtual bool isDebugVisualizationEnabled() const = 0;
    virtual void setDebugVisualizationEnabled(bool enabled) = 0;

    // Tile bounds visualization (shows tile boundaries as wireframe)
    virtual bool isTileBoundsVisualizationEnabled() const = 0;
    virtual void setTileBoundsVisualizationEnabled(bool enabled) = 0;

    // LOD strategy access (read-only for display)
    virtual const IGrassLODStrategy* getLODStrategy() const = 0;

    // Configuration
    virtual uint32_t getMaxLoadsPerFrame() const = 0;
    virtual void setMaxLoadsPerFrame(uint32_t max) = 0;

    // Get per-LOD tile size (for display)
    virtual float getTileSizeForLOD(uint32_t lod) const = 0;

    // Get num LOD levels
    virtual uint32_t getNumLODLevels() const = 0;
};
